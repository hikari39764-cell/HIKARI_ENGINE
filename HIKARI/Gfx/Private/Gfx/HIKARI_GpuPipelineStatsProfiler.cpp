#include "Gfx/HIKARI_GpuPipelineStatsProfiler.h"

#include <array>
#include <string>
#include <string_view>

#include <d3dx12.h>
#include <wrl.h>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_MetricRegistry.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_VideoMemory.h"

namespace HIKARI::GFX::GPU_PIPELINE_STATS {

namespace {
using Microsoft::WRL::ComPtr;

constexpr uint32_t kBufferedFrameCount = 3u;
constexpr uint32_t kPassCount = static_cast<uint32_t>(GPU_PROFILE::Pass::Count);
constexpr uint32_t kQueryCount = GPU_PROFILE::kMaxExecutionsPerFrame;
constexpr uint32_t kNoExecution = UINT32_MAX;
constexpr const char* kMetricOwner = "Gfx.GpuPipelineStats";

struct FrameSlot {
    ComPtr<ID3D12QueryHeap> queryHeap{};
    ComPtr<ID3D12Resource> readbackBuffer{};
    // 実行順。i 番目の実行は query i を使う。同じ pass も実行のたびに 1 つずつ使う。
    std::array<GPU_PROFILE::Pass, kQueryCount> executions{};
    uint32_t executionCount = 0;
    // 実行中の pass が使っている query。実行中でなければ kNoExecution。
    std::array<uint32_t, kPassCount> activeExecution{};
    bool resolved = false;
    uint64_t frameIndex = 0;
    uint64_t retireFence = 0;
    uint64_t statisticsEpoch = 0;
};

struct ProfilerState {
    std::array<FrameSlot, kBufferedFrameCount> slots{};
    FrameSlot* currentSlot = nullptr;
    uint32_t writeSlotIndex = 0;
    uint64_t statisticsEpoch = 1u;
    bool initialized = false;
    bool enabled = true;
    bool executionOverflowReported = false;
    const char* unavailableReason = "";
};

ProfilerState gState{};

// pass 1 つ分の指標。その pass が初めて計測された時に登録する。
struct PassMetrics {
    METRIC::MetricHandle calls = METRIC::kInvalidMetric;
    METRIC::MetricHandle asInvocations = METRIC::kInvalidMetric;
    METRIC::MetricHandle msInvocations = METRIC::kInvalidMetric;
    METRIC::MetricHandle msPrimitives = METRIC::kInvalidMetric;
    METRIC::MetricHandle clipPrimitives = METRIC::kInvalidMetric;
    METRIC::MetricHandle psInvocations = METRIC::kInvalidMetric;
};
std::array<PassMetrics, kPassCount> gPassMetrics{};

uint32_t PassIndex(GPU_PROFILE::Pass pass) { return static_cast<uint32_t>(pass); }

bool IsValidPass(GPU_PROFILE::Pass pass) { return PassIndex(pass) < kPassCount; }

METRIC::MetricHandle ExecutionsMetric() {
    static const METRIC::MetricHandle handle =
        METRIC::Register({METRIC::kGpuPipelineExecutions, kMetricOwner, "Gfx", "count", METRIC::MetricCategory::Count,
                          "Pipeline statistics queries recorded in the frame."});
    return handle;
}

const PassMetrics& MetricsOf(GPU_PROFILE::Pass pass) {
    PassMetrics& metrics = gPassMetrics[PassIndex(pass)];
    if (metrics.calls == METRIC::kInvalidMetric) {
        const std::string base = std::string("gpu.") + GPU_PROFILE::ToString(pass) + ".pipeline.";
        const char* module = GPU_PROFILE::ModuleOf(pass);
        const auto add = [&base, module](const char* item, const char* help) {
            return METRIC::Register({base + item, kMetricOwner, module, "count", METRIC::MetricCategory::Count, help});
        };
        metrics.calls = add("calls", "Pipeline statistics queries of the pass in the frame.");
        metrics.asInvocations = add("asInvocations", "Amplification shader invocations, summed over the frame.");
        metrics.msInvocations = add("msInvocations", "Mesh shader invocations, summed over the frame.");
        metrics.msPrimitives = add("msPrimitives", "Primitives emitted by mesh shaders, summed over the frame.");
        metrics.clipPrimitives = add("clipPrimitives", "Primitives that reached the clipper, summed over the frame.");
        metrics.psInvocations = add("psInvocations", "Pixel shader invocations, summed over the frame.");
    }
    return metrics;
}

// 管線統計が取れない理由を指標へ出す。登録済みの指標がすべて「非対応」になる。
void MarkUnavailable(const char* reason) {
    const std::string_view text = reason != nullptr ? reason : "";
    METRIC::MarkUnsupported(ExecutionsMetric(), text);
    for (const PassMetrics& metrics : gPassMetrics) {
        for (const METRIC::MetricHandle handle :
             {metrics.calls, metrics.asInvocations, metrics.msInvocations, metrics.msPrimitives, metrics.clipPrimitives,
              metrics.psInvocations}) {
            METRIC::MarkUnsupported(handle, text);
        }
    }
}

// 指標を捨てて、次の完了フレームを待つ状態へ戻す。取れない構成ならその理由は残す。
void ClearMetrics() {
    METRIC::ClearOwner(kMetricOwner);
    if (!gState.enabled) {
        MarkUnavailable(gState.unavailableReason);
    }
}

bool CheckPipelineStatsSupport(ID3D12Device* device) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9{};
    const HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS9, &options9, sizeof(options9));
    return SUCCEEDED(hr) && options9.MeshShaderPipelineStatsSupported != FALSE;
}

bool EnsureInitialized(ID3D12Device* device) {
    if (gState.initialized) {
        return true;
    }
    if (device == nullptr) {
        MarkUnavailable("Pipeline stats profiler requires a valid device.");
        return false;
    }

    if (!CheckPipelineStatsSupport(device)) {
        MarkUnavailable("Mesh shader pipeline statistics are not supported by this device/runtime.");
        return false;
    }

    for (FrameSlot& slot : gState.slots) {
        D3D12_QUERY_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1;
        heapDesc.Count = kQueryCount;
        if (!HIKARI_DX_CHECK(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(slot.queryHeap.GetAddressOf())),
                             "GpuPipelineStatsProfiler::CreateQueryHeap")) {
            MarkUnavailable("Pipeline stats query resources could not be created.");
            return false;
        }

        const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        const auto bufferDesc =
            CD3DX12_RESOURCE_DESC::Buffer(sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1) * kQueryCount);
        if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                                 device, &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                 nullptr, GFX::VIDEO_MEMORY::Owner::Diagnostics,
                                 IID_PPV_ARGS(slot.readbackBuffer.GetAddressOf())),
                             "GpuPipelineStatsProfiler::CreateReadbackBuffer")) {
            MarkUnavailable("Pipeline stats query resources could not be created.");
            return false;
        }
        slot.readbackBuffer->SetName(L"HIKARI.GpuPipelineStatsProfiler.Readback");
    }

    gState.initialized = true;
    ClearMetrics();
    return true;
}

void CollectResolvedSlot(FrameSlot& slot) {
    if (!slot.resolved || slot.readbackBuffer == nullptr) {
        return;
    }
    if (slot.statisticsEpoch != gState.statisticsEpoch) {
        slot.resolved = false;
        return;
    }

    const D3D12_RANGE readRange{0, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1) * slot.executionCount};
    void* mapped = nullptr;
    if (FAILED(slot.readbackBuffer->Map(0, &readRange, &mapped)) || mapped == nullptr) {
        return;
    }
    const auto* queryData = static_cast<const D3D12_QUERY_DATA_PIPELINE_STATISTICS1*>(mapped);

    // 同じ pass の実行は合計する。
    struct PassTotal {
        uint32_t calls = 0;
        uint64_t asInvocations = 0;
        uint64_t msInvocations = 0;
        uint64_t msPrimitives = 0;
        uint64_t clipPrimitives = 0;
        uint64_t psInvocations = 0;
    };
    std::array<PassTotal, kPassCount> totals{};
    for (uint32_t run = 0; run < slot.executionCount; ++run) {
        const D3D12_QUERY_DATA_PIPELINE_STATISTICS1& counters = queryData[run];
        PassTotal& total = totals[PassIndex(slot.executions[run])];
        ++total.calls;
        total.asInvocations += counters.ASInvocations;
        total.msInvocations += counters.MSInvocations;
        total.msPrimitives += counters.MSPrimitives;
        total.clipPrimitives += counters.CPrimitives;
        total.psInvocations += counters.PSInvocations;
    }

    const D3D12_RANGE writeRange{0, 0};
    slot.readbackBuffer->Unmap(0, &writeRange);
    slot.resolved = false;

    METRIC::Publish(ExecutionsMetric(), slot.executionCount, slot.frameIndex);
    for (uint32_t i = 0; i < kPassCount; ++i) {
        const PassTotal& total = totals[i];
        if (total.calls == 0u) {
            continue;
        }
        const PassMetrics& metrics = MetricsOf(static_cast<GPU_PROFILE::Pass>(i));
        METRIC::Publish(metrics.calls, total.calls, slot.frameIndex);
        METRIC::Publish(metrics.asInvocations, static_cast<double>(total.asInvocations), slot.frameIndex);
        METRIC::Publish(metrics.msInvocations, static_cast<double>(total.msInvocations), slot.frameIndex);
        METRIC::Publish(metrics.msPrimitives, static_cast<double>(total.msPrimitives), slot.frameIndex);
        METRIC::Publish(metrics.clipPrimitives, static_cast<double>(total.clipPrimitives), slot.frameIndex);
        METRIC::Publish(metrics.psInvocations, static_cast<double>(total.psInvocations), slot.frameIndex);
    }
}

void ResetSlot(FrameSlot& slot, uint64_t frameIndex) {
    slot.executionCount = 0;
    slot.activeExecution.fill(kNoExecution);
    slot.frameIndex = frameIndex;
    slot.resolved = false;
    slot.statisticsEpoch = gState.statisticsEpoch;
}
} // namespace

void SetEnabled(bool enabled, const char* unavailableReason) {
    // 「非対応」の理由を読み手が見つけられるよう、実行数の指標は最初の呼び出しで必ず登録する。
    (void)ExecutionsMetric();
    const bool changed = gState.enabled != enabled;
    const char* reason = enabled ? "" : (unavailableReason != nullptr ? unavailableReason : "");
    const bool reasonChanged = std::string_view(gState.unavailableReason) != reason;
    if (changed) {
        ++gState.statisticsEpoch;
    }
    gState.enabled = enabled;
    gState.unavailableReason = reason;
    if (!enabled) {
        gState.currentSlot = nullptr;
        if (changed || reasonChanged) {
            MarkUnavailable(reason);
        }
    } else if (changed) {
        ClearMetrics();
    }
}

void ResetStatistics() {
    ++gState.statisticsEpoch;
    // アクティブなクエリ状態は変更せず、フレームの解決後に世代番号で破棄する。
    ClearMetrics();
}

bool IsEnabled() { return gState.enabled; }

void BeginFrame(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, uint64_t frameIndex, uint64_t completedFence,
                uint64_t retireFence) {
    if (!gState.enabled) {
        gState.currentSlot = nullptr;
        return;
    }
    if (cmd == nullptr || !EnsureInitialized(device)) {
        gState.currentSlot = nullptr;
        return;
    }

    FrameSlot& slot = gState.slots[gState.writeSlotIndex % kBufferedFrameCount];
    if (completedFence == UINT64_MAX || retireFence == 0 || completedFence < slot.retireFence) {
        gState.currentSlot = nullptr;
        return;
    }
    CollectResolvedSlot(slot);
    ResetSlot(slot, frameIndex);
    slot.retireFence = retireFence;
    gState.currentSlot = &slot;
    ++gState.writeSlotIndex;
}

void EndFrame(ID3D12GraphicsCommandList* cmd) {
    FrameSlot* slot = gState.currentSlot;
    if (!gState.enabled || !gState.initialized || slot == nullptr || cmd == nullptr || slot->queryHeap == nullptr) {
        gState.currentSlot = nullptr;
        return;
    }

    // 閉じられなかった pass は、フレームの終わりで閉じる。
    for (uint32_t& execution : slot->activeExecution) {
        if (execution != kNoExecution) {
            cmd->EndQuery(slot->queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, execution);
            execution = kNoExecution;
        }
    }
    if (slot->executionCount > 0) {
        cmd->ResolveQueryData(slot->queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, 0u, slot->executionCount,
                              slot->readbackBuffer.Get(), 0u);
    }

    slot->resolved = true;
    gState.currentSlot = nullptr;
}

void Shutdown() {
    for (FrameSlot& slot : gState.slots) {
        slot.queryHeap.Reset();
        slot.readbackBuffer.Reset();
        slot.resolved = false;
    }
    gState = {};
    METRIC::ClearOwner(kMetricOwner);
}

bool BeginPass(ID3D12GraphicsCommandList* cmd, GPU_PROFILE::Pass pass) {
    FrameSlot* slot = gState.currentSlot;
    if (!gState.enabled || !gState.initialized || slot == nullptr || cmd == nullptr || !IsValidPass(pass)) {
        return false;
    }

    // 実行中の同じ pass を入れ子で計ると二重計上になる。終わった後の再実行は 1 回ずつ計る。
    const uint32_t index = PassIndex(pass);
    if (slot->activeExecution[index] != kNoExecution || slot->queryHeap == nullptr) {
        return false;
    }
    if (slot->executionCount == kQueryCount) {
        if (!gState.executionOverflowReported) {
            gState.executionOverflowReported = true;
            HIKARI_LOG_WARN("[GpuPipelineStatsProfiler] more than " + std::to_string(kQueryCount) +
                            " pipeline statistics executions in one frame; the rest are not counted.");
        }
        return false;
    }

    const uint32_t run = slot->executionCount++;
    cmd->BeginQuery(slot->queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, run);
    slot->executions[run] = pass;
    slot->activeExecution[index] = run;
    return true;
}

void EndPass(ID3D12GraphicsCommandList* cmd, GPU_PROFILE::Pass pass) {
    FrameSlot* slot = gState.currentSlot;
    if (!gState.enabled || !gState.initialized || slot == nullptr || cmd == nullptr || !IsValidPass(pass)) {
        return;
    }

    const uint32_t index = PassIndex(pass);
    if (slot->activeExecution[index] == kNoExecution || slot->queryHeap == nullptr) {
        return;
    }

    cmd->EndQuery(slot->queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, slot->activeExecution[index]);
    slot->activeExecution[index] = kNoExecution;
}

ScopedPipelineStats::ScopedPipelineStats(ID3D12GraphicsCommandList* cmd, GPU_PROFILE::Pass pass)
    : cmd_(cmd), pass_(pass) {
    if (cmd_ != nullptr && IsValidPass(pass_)) {
        active_ = GPU_PIPELINE_STATS::BeginPass(cmd_, pass_);
    }
}

ScopedPipelineStats::~ScopedPipelineStats() {
    if (active_) {
        GPU_PIPELINE_STATS::EndPass(cmd_, pass_);
    }
}

} // namespace HIKARI::GFX::GPU_PIPELINE_STATS
