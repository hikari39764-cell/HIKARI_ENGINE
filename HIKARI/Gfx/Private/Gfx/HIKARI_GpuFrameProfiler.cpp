#include "Gfx/HIKARI_GpuFrameProfiler.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <string>
#include <string_view>

#include <d3dx12.h>
#include <wrl.h>

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_MetricRegistry.h"
#include "Diagnostics/HIKARI_TraceRecorder.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_VideoMemory.h"

namespace HIKARI::GFX::GPU_PROFILE {

namespace {
using Microsoft::WRL::ComPtr;

constexpr uint32_t kBufferedFrameCount = 3u;
constexpr uint32_t kPassCount = static_cast<uint32_t>(Pass::Count);
constexpr uint32_t kQueriesPerExecution = 2u;
constexpr uint32_t kQueryCount = kMaxExecutionsPerFrame * kQueriesPerExecution;
constexpr uint32_t kNoExecution = UINT32_MAX;
constexpr const char* kMetricOwner = "Gfx.GpuFrameProfiler";

struct Execution {
    Pass pass = Pass::Count;
    Pass parent = Pass::Count;
};

struct FrameSlot {
    ComPtr<ID3D12QueryHeap> queryHeap{};
    ComPtr<ID3D12Resource> readbackBuffer{};
    // 実行順。i 番目の実行は query の 2i（開始）と 2i + 1（終了）を使う。
    std::array<Execution, kMaxExecutionsPerFrame> executions{};
    uint32_t executionCount = 0;
    // 実行中の pass が使っている executions の添字。実行中でなければ kNoExecution。
    std::array<uint32_t, kPassCount> activeExecution{};
    std::array<Pass, kPassCount> activeStack{};
    uint32_t activeDepth = 0;
    bool resolved = false;
    uint64_t frameIndex = 0;
    uint64_t retireFence = 0;
    uint64_t statisticsEpoch = 0;
};

struct ProfilerState {
    std::array<FrameSlot, kBufferedFrameCount> slots{};
    FrameSlot* currentSlot = nullptr;
    uint64_t timestampFrequency = 0;
    uint32_t writeSlotIndex = 0;
    uint32_t groupDepth = 0;
    uint64_t statisticsEpoch = 1u;
    bool initialized = false;
    bool enabled = true;
    bool executionOverflowReported = false;
    const char* unavailableReason = "";
};

ProfilerState gState{};

// pass 1 つ分の指標。その pass が初めて計測された時に登録する。
struct PassMetrics {
    METRIC::MetricHandle ms = METRIC::kInvalidMetric;
    METRIC::MetricHandle calls = METRIC::kInvalidMetric;
    METRIC::MetricHandle selfMs = METRIC::kInvalidMetric;
};
std::array<PassMetrics, kPassCount> gPassMetrics{};

uint32_t PassIndex(Pass pass) { return static_cast<uint32_t>(pass); }

uint32_t QueryIndex(uint32_t execution, uint32_t endpoint) { return execution * kQueriesPerExecution + endpoint; }

bool IsValidPass(Pass pass) { return PassIndex(pass) < kPassCount; }

METRIC::MetricHandle FrameMetric() {
    static const METRIC::MetricHandle handle =
        METRIC::Register({METRIC::kFrameGpuMs, kMetricOwner, ModuleOf(Pass::CommandFrame), "ms",
                          METRIC::MetricCategory::Work, "GPU time of the whole frame's commands."});
    return handle;
}

const PassMetrics& MetricsOf(Pass pass) {
    PassMetrics& metrics = gPassMetrics[PassIndex(pass)];
    if (metrics.ms == METRIC::kInvalidMetric) {
        const std::string base = std::string("gpu.") + ToString(pass);
        const char* module = ModuleOf(pass);
        metrics.ms = METRIC::Register({base + ".ms", kMetricOwner, module, "ms", METRIC::MetricCategory::Work,
                                       "GPU time of the pass, summed over every execution in the frame."});
        metrics.calls = METRIC::Register({base + ".calls", kMetricOwner, module, "count", METRIC::MetricCategory::Count,
                                          "Executions of the pass in the frame."});
        metrics.selfMs = METRIC::Register({base + ".selfMs", kMetricOwner, module, "ms", METRIC::MetricCategory::Work,
                                           "GPU time of the pass without the passes nested inside it."});
    }
    return metrics;
}

// 計時が取れない理由を指標へ出す。登録済みの指標がすべて「非対応」になる。
void MarkUnavailable(const char* reason) {
    const std::string_view text = reason != nullptr ? reason : "";
    METRIC::MarkUnsupported(FrameMetric(), text);
    for (const PassMetrics& metrics : gPassMetrics) {
        METRIC::MarkUnsupported(metrics.ms, text);
        METRIC::MarkUnsupported(metrics.calls, text);
        METRIC::MarkUnsupported(metrics.selfMs, text);
    }
}

// 時間線の記録用に、GPU のタイムスタンプを CPU の時計（steady_clock と同じ QPC）へ写す 1 対を取る。
// 取れない時は周波数だけを渡す（GPU の時計のまま、先頭だけ CPU 側に揃えて書かれる）。
void CalibrateTraceClock(ID3D12CommandQueue* queue) {
    TRACING::GpuClock clock{};
    clock.ticksPerSecond = gState.timestampFrequency;
    uint64_t gpuTicks = 0;
    uint64_t cpuTicks = 0;
    LARGE_INTEGER counterFrequency{};
    if (queue != nullptr && SUCCEEDED(queue->GetClockCalibration(&gpuTicks, &cpuTicks)) &&
        QueryPerformanceFrequency(&counterFrequency) && counterFrequency.QuadPart > 0) {
        const uint64_t perSecond = static_cast<uint64_t>(counterFrequency.QuadPart);
        clock.gpuTicks = gpuTicks;
        clock.cpuNanoseconds = static_cast<int64_t>((cpuTicks / perSecond) * 1000000000ull +
                                                    (cpuTicks % perSecond) * 1000000000ull / perSecond);
        clock.calibrated = true;
    }
    TRACING::SetGpuClock(clock);
}

// 指標と標本窓を捨てて、次の完了フレームを待つ状態へ戻す。計時できない構成ならその理由は残す。
void ClearMetrics() {
    METRIC::ClearOwner(kMetricOwner);
    if (!gState.enabled) {
        MarkUnavailable(gState.unavailableReason);
    }
}

bool EnsureInitialized(ID3D12Device* device, ID3D12CommandQueue* queue) {
    if (gState.initialized) {
        return true;
    }
    if (device == nullptr || queue == nullptr) {
        MarkUnavailable("GPU profiler requires a valid device and command queue.");
        return false;
    }

    uint64_t frequency = 0;
    if (FAILED(queue->GetTimestampFrequency(&frequency)) || frequency == 0) {
        HIKARI_LOG_WARN("[GpuFrameProfiler] timestamp frequency is unavailable.");
        MarkUnavailable("Timestamp frequency is unavailable.");
        return false;
    }

    for (FrameSlot& slot : gState.slots) {
        D3D12_QUERY_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heapDesc.Count = kQueryCount;
        if (!HIKARI_DX_CHECK(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(slot.queryHeap.GetAddressOf())),
                             "GpuFrameProfiler::CreateQueryHeap")) {
            MarkUnavailable("GPU profiler query resources could not be created.");
            return false;
        }

        const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint64_t) * kQueryCount);
        if (!HIKARI_DX_CHECK(GFX::VIDEO_MEMORY::CreateCommittedResource(
                                 device, &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                 nullptr, GFX::VIDEO_MEMORY::Owner::Diagnostics,
                                 IID_PPV_ARGS(slot.readbackBuffer.GetAddressOf())),
                             "GpuFrameProfiler::CreateReadbackBuffer")) {
            MarkUnavailable("GPU profiler query resources could not be created.");
            return false;
        }
        slot.readbackBuffer->SetName(L"HIKARI.GpuFrameProfiler.Readback");
    }

    gState.timestampFrequency = frequency;
    gState.initialized = true;
    ClearMetrics();
    return true;
}

void CollectResolvedSlot(FrameSlot& slot) {
    if (!slot.resolved || slot.readbackBuffer == nullptr || gState.timestampFrequency == 0) {
        return;
    }
    if (slot.statisticsEpoch != gState.statisticsEpoch) {
        slot.resolved = false;
        return;
    }

    const D3D12_RANGE readRange{0, sizeof(uint64_t) * slot.executionCount * kQueriesPerExecution};
    void* mapped = nullptr;
    if (FAILED(slot.readbackBuffer->Map(0, &readRange, &mapped)) || mapped == nullptr) {
        return;
    }
    const uint64_t* queryData = static_cast<const uint64_t*>(mapped);

    // 同じ pass の実行は合計する。self 時間は、実行ごとに自分の親 pass の分から引く。
    struct PassTotal {
        double ms = 0.0;
        double selfMs = 0.0;
        uint32_t calls = 0;
    };
    std::array<PassTotal, kPassCount> totals{};
    for (uint32_t run = 0; run < slot.executionCount; ++run) {
        const uint64_t start = queryData[QueryIndex(run, 0u)];
        const uint64_t end = queryData[QueryIndex(run, 1u)];
        if (end < start) {
            continue;
        }

        const Execution& execution = slot.executions[run];
        if (TRACING::IsRecording()) {
            TRACING::RecordGpu(ToString(execution.pass), ModuleOf(execution.pass),
                               IsValidPass(execution.parent) ? ToString(execution.parent) : nullptr, start, end,
                               slot.frameIndex);
        }
        const double ms = static_cast<double>(end - start) * 1000.0 / static_cast<double>(gState.timestampFrequency);
        PassTotal& total = totals[PassIndex(execution.pass)];
        ++total.calls;
        total.ms += ms;
        total.selfMs += ms;
        if (IsValidPass(execution.parent)) {
            totals[PassIndex(execution.parent)].selfMs -= ms;
        }
    }

    const D3D12_RANGE writeRange{0, 0};
    slot.readbackBuffer->Unmap(0, &writeRange);
    slot.resolved = false;

    // 登録表の標本窓へ入れる標本は、フレームごとの合計。
    for (uint32_t i = 0; i < kPassCount; ++i) {
        const PassTotal& total = totals[i];
        if (total.calls == 0u) {
            continue;
        }
        const Pass pass = static_cast<Pass>(i);
        if (pass == Pass::CommandFrame) {
            METRIC::Publish(FrameMetric(), total.ms, slot.frameIndex);
            continue;
        }
        const PassMetrics& metrics = MetricsOf(pass);
        METRIC::Publish(metrics.ms, total.ms, slot.frameIndex);
        METRIC::Publish(metrics.calls, total.calls, slot.frameIndex);
        METRIC::Publish(metrics.selfMs, (std::max)(0.0, total.selfMs), slot.frameIndex);
    }
}

void ResetSlot(FrameSlot& slot, uint64_t frameIndex) {
    slot.executionCount = 0;
    slot.activeExecution.fill(kNoExecution);
    slot.activeDepth = 0;
    slot.frameIndex = frameIndex;
    slot.resolved = false;
    slot.statisticsEpoch = gState.statisticsEpoch;
}
} // namespace

void SetEnabled(bool enabled, const char* unavailableReason) {
    // 「非対応」の理由を読み手が見つけられるよう、フレーム指標は最初の呼び出しで必ず登録する。
    (void)FrameMetric();
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
    // 記録中のスコープは維持し、EndPass/EndFrame まで完了させる。
    // ただし、この世代の結果は統計リセット後の値として採用しない。
    ClearMetrics();
}

bool IsEnabled() { return gState.enabled; }

namespace {
// pass の表。名前と所属モジュールを 1 行にまとめる（登録表の id と時間線の cat になる）。列挙の順に並べる。
struct PassInfo {
    Pass pass;
    const char* name;
    const char* module;
};
constexpr PassInfo kPassInfo[] = {
    {Pass::ShadowMap, "ShadowMap", "Render3D"},
    {Pass::GeometryAux, "GeometryAux", "Render3D"},
    {Pass::ClusterCull, "Cluster Cull", "Render3D"},
    {Pass::DepthPrepass, "Depth Prepass", "Render3D"},
    {Pass::TraditionalDrawGeometryAux, "Traditional Draw GeometryAux", "Render3D"},
    {Pass::TraditionalDrawForward, "Traditional Draw Forward", "Render3D"},
    {Pass::MeshletDrawGeometryAux, "Meshlet Draw GeometryAux", "Render3D"},
    {Pass::MeshletDrawDepthPrepass, "Meshlet Draw DepthPrepass", "Render3D"},
    {Pass::MeshletDrawForward, "Meshlet Draw Forward", "Render3D"},
    {Pass::MeshletDrawDepthAware, "Meshlet Draw DepthAware", "Render3D"},
    {Pass::MeshletDrawTransparent, "Meshlet Draw Transparent", "Render3D"},
    {Pass::MeshletDrawGeometryOutline, "Meshlet Draw Geometry Outline", "Render3D"},
    {Pass::MeshletDrawVisibilityOverlay, "Meshlet Draw Visibility Overlay", "Render3D"},
    {Pass::TraditionalDrawShadow, "Traditional Draw Shadow", "Render3D"},
    {Pass::MeshletDrawShadow, "Meshlet Draw Shadow", "Render3D"},
    {Pass::TraditionalDrawShadowStatic, "Traditional Draw Shadow Static", "Render3D"},
    {Pass::MeshletDrawShadowStatic, "Meshlet Draw Shadow Static", "Render3D"},
    {Pass::TraditionalDrawShadowDynamic, "Traditional Draw Shadow Dynamic", "Render3D"},
    {Pass::MeshletDrawShadowDynamic, "Meshlet Draw Shadow Dynamic", "Render3D"},
    {Pass::TraditionalDrawShadowFallback, "Traditional Draw Shadow Fallback", "Render3D"},
    {Pass::MeshletDrawShadowFallback, "Meshlet Draw Shadow Fallback", "Render3D"},
    {Pass::SsaoMain, "SSAO Main", "Render3D"},
    {Pass::SsaoBlur, "SSAO Blur", "Render3D"},
    {Pass::ScreenSpaceReflections, "Screen Space Reflections", "Render3D"},
    {Pass::SubsurfaceScattering, "Subsurface Scattering", "Render3D"},
    {Pass::RayTracedShadow, "Ray Traced Shadow", "Render3D"},
    {Pass::TemporalMotionVectors, "Temporal MotionVectors", "Render3D"},
    {Pass::TemporalTaaResolve, "Temporal TAA", "Render3D"},
    {Pass::TemporalDlaaResolve, "Temporal DLAA", "Render3D"},
    {Pass::TemporalDlssResolve, "Temporal DLSS", "Render3D"},
    {Pass::ForwardOpaque, "ForwardOpaque", "Render3D"},
    {Pass::DepthAware, "DepthAware", "Render3D"},
    {Pass::ForwardTransparent, "ForwardTransparent", "Render3D"},
    {Pass::DepthOfField, "Depth Of Field", "Render3D"},
    {Pass::MotionBlur, "Motion Blur", "Render3D"},
    {Pass::ParticleSimulation, "Particle Simulation", "Vfx"},
    {Pass::ParticleDraw, "Particle Draw", "Vfx"},
    {Pass::VolumetricLighting, "Volumetric Lighting", "Render3D"},
    {Pass::StylizedGeometryOutline, "Stylized Geometry Outline", "Render3D"},
    {Pass::StylizedVisibilityOverlay, "Stylized Visibility Overlay", "Render3D"},
    {Pass::StylizedScreenFeatures, "Stylized Screen Features", "Render3D"},
    {Pass::PostSceneResolve, "Post Scene Resolve", "Post"},
    {Pass::PresentationComposition, "Presentation Composition", "Runtime"},
    {Pass::GameViewResolve, "GameView Resolve", "Post"},
    {Pass::SceneLayers, "Scene Layers", "Runtime"},
    {Pass::UiLayers, "UI Layers", "Render2D"},
    {Pass::ImGui, "ImGui", "Editor"},
    {Pass::WaterVisibilityDepth, "Water Visibility Depth", "Render3D"},
    {Pass::VolumetricClouds, "Volumetric Clouds", "Render3D"},
    {Pass::SceneCaptures, "Scene Captures", "Scene"},
    {Pass::CommandFrame, "Command Frame", "Gfx"},
    {Pass::EnvironmentLighting, "Environment Lighting", "Render3D"},
    {Pass::DynamicDiffuseGi, "Dynamic Diffuse GI", "Render3D"},
    {Pass::CloudSolarVisibility, "Cloud Solar Visibility", "Render3D"},
    {Pass::RuntimeRayScenePreparation, "Shared Ray Scene Preparation", "Render3D"},
    {Pass::MeshletForwardStandard, "Meshlet Forward Standard PBR", "Render3D"},
    {Pass::MeshletForwardMaterialProgram, "Meshlet Forward Material Program", "Render3D"},
    {Pass::MeshletForwardTerrain, "Meshlet Forward Terrain", "Render3D"},
    {Pass::GameUi, "Game UI", "Render3D"},
};
constexpr bool PassInfoFollowsEnum() {
    for (size_t index = 0; index < std::size(kPassInfo); ++index) {
        if (static_cast<size_t>(kPassInfo[index].pass) != index) {
            return false;
        }
    }
    return true;
}
static_assert(std::size(kPassInfo) == static_cast<size_t>(Pass::Count), "every Pass needs a row");
static_assert(PassInfoFollowsEnum(), "kPassInfo must follow the Pass order");
} // namespace

const char* ToString(Pass pass) { return IsValidPass(pass) ? kPassInfo[PassIndex(pass)].name : ""; }

const char* ModuleOf(Pass pass) { return IsValidPass(pass) ? kPassInfo[PassIndex(pass)].module : ""; }

void BeginFrame(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* cmd, uint64_t frameIndex,
                uint64_t completedFence, uint64_t retireFence) {
    if (!gState.enabled) {
        gState.currentSlot = nullptr;
        return;
    }
    if (cmd == nullptr || !EnsureInitialized(device, queue)) {
        gState.currentSlot = nullptr;
        return;
    }
    if (TRACING::IsRecording() && !TRACING::HasGpuClock()) {
        CalibrateTraceClock(queue);
    }

    FrameSlot& slot = gState.slots[gState.writeSlotIndex % kBufferedFrameCount];
    // 計測のために待機しない。未完了の query/readback はそのまま保持する。
    if (completedFence == UINT64_MAX || retireFence == 0 || completedFence < slot.retireFence) {
        gState.currentSlot = nullptr;
        return;
    }
    // 前回この slot に解決した timestamp を読み、今フレーム用に再利用する。
    CollectResolvedSlot(slot);
    ResetSlot(slot, frameIndex);
    slot.retireFence = retireFence;
    gState.currentSlot = &slot;
    ++gState.writeSlotIndex;
    BeginPass(cmd, Pass::CommandFrame);
}

void EndFrame(ID3D12GraphicsCommandList* cmd) {
    FrameSlot* slot = gState.currentSlot;
    if (!gState.enabled || !gState.initialized || slot == nullptr || cmd == nullptr || slot->queryHeap == nullptr) {
        gState.currentSlot = nullptr;
        return;
    }

    EndPass(cmd, Pass::CommandFrame);
    // 閉じられなかった pass は、フレームの終わりで閉じる。
    for (uint32_t& execution : slot->activeExecution) {
        if (execution != kNoExecution) {
            cmd->EndQuery(slot->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, QueryIndex(execution, 1u));
            execution = kNoExecution;
        }
    }
    if (slot->executionCount > 0) {
        cmd->ResolveQueryData(slot->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0u,
                              slot->executionCount * kQueriesPerExecution, slot->readbackBuffer.Get(), 0u);
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

bool BeginPass(ID3D12GraphicsCommandList* cmd, Pass pass) {
    if (gState.groupDepth > 0)
        return false;
    FrameSlot* slot = gState.currentSlot;
    if (!gState.enabled || !gState.initialized || slot == nullptr || cmd == nullptr || !IsValidPass(pass)) {
        return false;
    }

    // 実行中の同じ pass を入れ子で計ると二重計上になる。終わった後の再実行は 1 回ずつ計る。
    const uint32_t index = PassIndex(pass);
    if (slot->activeExecution[index] != kNoExecution || slot->queryHeap == nullptr) {
        return false;
    }
    if (slot->executionCount == kMaxExecutionsPerFrame) {
        if (!gState.executionOverflowReported) {
            gState.executionOverflowReported = true;
            HIKARI_LOG_WARN("[GpuFrameProfiler] more than " + std::to_string(kMaxExecutionsPerFrame) +
                            " GPU pass executions in one frame; the rest are not timed.");
        }
        return false;
    }

    const uint32_t run = slot->executionCount++;
    cmd->EndQuery(slot->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, QueryIndex(run, 0u));
    slot->executions[run] = {pass, slot->activeDepth > 0u ? slot->activeStack[slot->activeDepth - 1u] : Pass::Count};
    slot->activeStack[slot->activeDepth++] = pass;
    slot->activeExecution[index] = run;
    return true;
}

void EndPass(ID3D12GraphicsCommandList* cmd, Pass pass) {
    FrameSlot* slot = gState.currentSlot;
    if (!gState.enabled || !gState.initialized || slot == nullptr || cmd == nullptr || !IsValidPass(pass)) {
        return;
    }

    const uint32_t index = PassIndex(pass);
    if (slot->activeExecution[index] == kNoExecution || slot->queryHeap == nullptr) {
        return;
    }

    cmd->EndQuery(slot->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, QueryIndex(slot->activeExecution[index], 1u));
    slot->activeExecution[index] = kNoExecution;
    for (uint32_t i = 0; i < slot->activeDepth; ++i) {
        if (slot->activeStack[i] != pass)
            continue;
        std::move(slot->activeStack.begin() + i + 1u, slot->activeStack.begin() + slot->activeDepth,
                  slot->activeStack.begin() + i);
        --slot->activeDepth;
        break;
    }
}

ScopedGpuTimer::ScopedGpuTimer(ID3D12GraphicsCommandList* cmd, Pass pass) : cmd_(cmd), pass_(pass) {
    if (cmd_ != nullptr && IsValidPass(pass_)) {
        active_ = BeginPass(cmd_, pass_);
    }
}

ScopedGpuTimer::~ScopedGpuTimer() {
    if (active_) {
        EndPass(cmd_, pass_);
    }
}

ScopedGpuTimerGroup::ScopedGpuTimerGroup(ID3D12GraphicsCommandList* cmd, Pass pass) : timer_(cmd, pass) {
    ++gState.groupDepth;
}

ScopedGpuTimerGroup::~ScopedGpuTimerGroup() {
    if (gState.groupDepth > 0)
        --gState.groupDepth;
}

} // namespace HIKARI::GFX::GPU_PROFILE
