#include "Gfx/HIKARI_GpuFrameProfiler.h"

#include <algorithm>
#include "Core/HIKARI_RollingTimingWindow.h"

#include <d3dx12.h>
#include <wrl.h>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DXCheck.h"

namespace HIKARI::GFX::GPU_PROFILE {

namespace {
using Microsoft::WRL::ComPtr;

constexpr uint32_t kBufferedFrameCount = 3u;
constexpr uint32_t kPassCount = static_cast<uint32_t>(Pass::Count);
constexpr uint32_t kQueriesPerExecution = 2u;
// 1 フレームで計れる実行回数の上限。同じ pass も実行のたびに 1 組ずつ使う。固定長なのでフレームごとの確保はない。
constexpr uint32_t kMaxExecutionsPerFrame = 512u;
constexpr uint32_t kQueryCount = kMaxExecutionsPerFrame * kQueriesPerExecution;
constexpr uint32_t kNoExecution = UINT32_MAX;

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
    FrameSnapshot latest{};
    std::array<PERFORMANCE::RollingTimingWindow, kPassCount> rolling{};
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

uint32_t PassIndex(Pass pass) { return static_cast<uint32_t>(pass); }

uint32_t QueryIndex(uint32_t execution, uint32_t endpoint) { return execution * kQueriesPerExecution + endpoint; }

bool IsValidPass(Pass pass) { return PassIndex(pass) < kPassCount; }

void ResetLatestNames(FrameSnapshot& snapshot) {
    for (uint32_t i = 0; i < kPassCount; ++i) {
        const Pass pass = static_cast<Pass>(i);
        snapshot.passes[i].name = ToString(pass);
    }
}

void ResetUnavailableSnapshot(const char* reason) {
    gState.latest = {};
    gState.latest.initialized = gState.initialized;
    gState.latest.profilerEnabled = gState.enabled;
    gState.latest.gpuTimingAvailable = false;
    gState.latest.timestampFrequency = gState.timestampFrequency;
    gState.latest.unavailableReason = reason != nullptr ? reason : "";
    ResetLatestNames(gState.latest);
}

void ResetStatisticsSnapshot() {
    gState.latest = {};
    gState.latest.initialized = gState.initialized;
    gState.latest.profilerEnabled = gState.enabled;
    gState.latest.gpuTimingAvailable = false;
    gState.latest.timestampFrequency = gState.timestampFrequency;
    gState.latest.unavailableReason =
        gState.enabled ? "Waiting for a completed GPU frame after statistics reset." : gState.unavailableReason;
    ResetLatestNames(gState.latest);
}

bool EnsureInitialized(ID3D12Device* device, ID3D12CommandQueue* queue) {
    if (gState.initialized) {
        return true;
    }
    if (device == nullptr || queue == nullptr) {
        ResetUnavailableSnapshot("GPU profiler requires a valid device and command queue.");
        return false;
    }

    uint64_t frequency = 0;
    if (FAILED(queue->GetTimestampFrequency(&frequency)) || frequency == 0) {
        HIKARI_LOG_WARN("[GpuFrameProfiler] timestamp frequency is unavailable.");
        ResetUnavailableSnapshot("Timestamp frequency is unavailable.");
        return false;
    }

    for (FrameSlot& slot : gState.slots) {
        D3D12_QUERY_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heapDesc.Count = kQueryCount;
        if (!HIKARI_DX_CHECK(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(slot.queryHeap.GetAddressOf())),
                             "GpuFrameProfiler::CreateQueryHeap")) {
            return false;
        }

        const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
        const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(uint64_t) * kQueryCount);
        if (!HIKARI_DX_CHECK(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                                             D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                             IID_PPV_ARGS(slot.readbackBuffer.GetAddressOf())),
                             "GpuFrameProfiler::CreateReadbackBuffer")) {
            return false;
        }
        slot.readbackBuffer->SetName(L"HIKARI.GpuFrameProfiler.Readback");
    }

    gState.timestampFrequency = frequency;
    gState.initialized = true;
    gState.latest = {};
    gState.latest.initialized = true;
    gState.latest.profilerEnabled = true;
    gState.latest.gpuTimingAvailable = true;
    gState.latest.timestampFrequency = frequency;
    gState.latest.unavailableReason = "";
    ResetLatestNames(gState.latest);
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

    FrameSnapshot snapshot{};
    snapshot.initialized = true;
    snapshot.profilerEnabled = true;
    snapshot.gpuTimingAvailable = true;
    snapshot.frameIndex = slot.frameIndex;
    snapshot.timestampFrequency = gState.timestampFrequency;
    snapshot.unavailableReason = "";
    ResetLatestNames(snapshot);

    // 同じ pass の実行は合計する。self 時間は、実行ごとに自分の親 pass の分から引く。
    for (uint32_t run = 0; run < slot.executionCount; ++run) {
        const uint64_t start = queryData[QueryIndex(run, 0u)];
        const uint64_t end = queryData[QueryIndex(run, 1u)];
        if (end < start) {
            continue;
        }

        const Execution& execution = slot.executions[run];
        PassTiming& timing = snapshot.passes[PassIndex(execution.pass)];
        const uint64_t ticks = end - start;
        const double ms = static_cast<double>(ticks) * 1000.0 / static_cast<double>(gState.timestampFrequency);
        if (!timing.valid) {
            timing.valid = true;
            timing.parentPass = execution.parent;
        }
        ++timing.callCount;
        timing.ticks += ticks;
        timing.gpuMs += ms;
        timing.exclusiveGpuMs += ms;
        if (IsValidPass(execution.parent)) {
            snapshot.passes[PassIndex(execution.parent)].exclusiveGpuMs -= ms;
        }
    }

    // 窓へ入れる標本は、フレームごとの合計。
    for (uint32_t i = 0; i < kPassCount; ++i) {
        PassTiming& timing = snapshot.passes[i];
        timing.exclusiveGpuMs = (std::max)(0.0, timing.exclusiveGpuMs);
        if (!timing.valid) {
            continue;
        }
        auto& window = gState.rolling[i];
        window.Push(timing.gpuMs);
        timing.averageMs = window.Average();
        timing.p95Ms = window.p95;
        timing.p99Ms = window.p99;
        timing.maximumMs = window.maximum;
        timing.rollingSampleCount = static_cast<uint32_t>(window.count);
    }

    const D3D12_RANGE writeRange{0, 0};
    slot.readbackBuffer->Unmap(0, &writeRange);
    slot.resolved = false;
    gState.latest = snapshot;
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
    const bool changed = gState.enabled != enabled;
    if (changed) {
        gState.rolling = {};
        ++gState.statisticsEpoch;
    }
    gState.enabled = enabled;
    gState.unavailableReason = enabled ? "" : (unavailableReason != nullptr ? unavailableReason : "");
    if (!enabled) {
        gState.currentSlot = nullptr;
        ResetUnavailableSnapshot(gState.unavailableReason);
    } else if (changed) {
        ResetStatisticsSnapshot();
    }
}

void ResetStatistics() {
    gState.rolling = {};
    ++gState.statisticsEpoch;
    // 記録中のスコープは維持し、EndPass/EndFrame まで完了させる。
    // ただし、この世代の結果は統計リセット後の値として採用しない。
    ResetStatisticsSnapshot();
}

bool IsEnabled() { return gState.enabled; }

const char* ToString(Pass pass) {
    switch (pass) {
    case Pass::ShadowMap: return "ShadowMap";
    case Pass::GeometryAux: return "GeometryAux";
    case Pass::ClusterCull: return "Cluster Cull";
    case Pass::DepthPrepass: return "Depth Prepass";
    case Pass::TraditionalDrawGeometryAux: return "Traditional Draw GeometryAux";
    case Pass::TraditionalDrawForward: return "Traditional Draw Forward";
    case Pass::MeshletDrawGeometryAux: return "Meshlet Draw GeometryAux";
    case Pass::MeshletDrawDepthPrepass: return "Meshlet Draw DepthPrepass";
    case Pass::MeshletDrawForward: return "Meshlet Draw Forward";
    case Pass::MeshletDrawDepthAware: return "Meshlet Draw DepthAware";
    case Pass::MeshletDrawTransparent: return "Meshlet Draw Transparent";
    case Pass::MeshletDrawGeometryOutline: return "Meshlet Draw Geometry Outline";
    case Pass::MeshletDrawVisibilityOverlay: return "Meshlet Draw Visibility Overlay";
    case Pass::TraditionalDrawShadow: return "Traditional Draw Shadow";
    case Pass::MeshletDrawShadow: return "Meshlet Draw Shadow";
    case Pass::TraditionalDrawShadowStatic: return "Traditional Draw Shadow Static";
    case Pass::MeshletDrawShadowStatic: return "Meshlet Draw Shadow Static";
    case Pass::TraditionalDrawShadowDynamic: return "Traditional Draw Shadow Dynamic";
    case Pass::MeshletDrawShadowDynamic: return "Meshlet Draw Shadow Dynamic";
    case Pass::TraditionalDrawShadowFallback: return "Traditional Draw Shadow Fallback";
    case Pass::MeshletDrawShadowFallback: return "Meshlet Draw Shadow Fallback";
    case Pass::SsaoMain: return "SSAO Main";
    case Pass::SsaoBlur: return "SSAO Blur";
    case Pass::ScreenSpaceReflections: return "Screen Space Reflections";
    case Pass::SubsurfaceScattering: return "Subsurface Scattering";
    case Pass::RayTracedShadow: return "Ray Traced Shadow";
    case Pass::TemporalMotionVectors: return "Temporal MotionVectors";
    case Pass::TemporalTaaResolve: return "Temporal TAA";
    case Pass::TemporalDlaaResolve: return "Temporal DLAA";
    case Pass::TemporalDlssResolve: return "Temporal DLSS";
    case Pass::ForwardOpaque: return "ForwardOpaque";
    case Pass::DepthAware: return "DepthAware";
    case Pass::ForwardTransparent: return "ForwardTransparent";
    case Pass::DepthOfField: return "Depth Of Field";
    case Pass::MotionBlur: return "Motion Blur";
    case Pass::ParticleSimulation: return "Particle Simulation";
    case Pass::ParticleDraw: return "Particle Draw";
    case Pass::VolumetricLighting: return "Volumetric Lighting";
    case Pass::StylizedGeometryOutline: return "Stylized Geometry Outline";
    case Pass::StylizedVisibilityOverlay: return "Stylized Visibility Overlay";
    case Pass::StylizedScreenFeatures: return "Stylized Screen Features";
    case Pass::PostSceneResolve: return "Post Scene Resolve";
    case Pass::PresentationComposition: return "Presentation Composition";
    case Pass::GameViewResolve: return "GameView Resolve";
    case Pass::SceneLayers: return "Scene Layers";
    case Pass::UiLayers: return "UI Layers";
    case Pass::ImGui: return "ImGui";
    case Pass::WaterVisibilityDepth: return "Water Visibility Depth";
    case Pass::VolumetricClouds: return "Volumetric Clouds";
    case Pass::EnvironmentLighting: return "Environment Lighting";
    case Pass::DynamicDiffuseGi: return "Dynamic Diffuse GI";
    case Pass::CloudSolarVisibility: return "Cloud Solar Visibility";
    case Pass::RuntimeRayScenePreparation: return "Shared Ray Scene Preparation";
    case Pass::SceneCaptures: return "Scene Captures";
    case Pass::CommandFrame: return "Command Frame";
    case Pass::Count:
    default: return "";
    }
}

void BeginFrame(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* cmd, uint64_t frameIndex,
                uint64_t completedFence, uint64_t retireFence) {
    if (!gState.enabled) {
        gState.currentSlot = nullptr;
        ResetUnavailableSnapshot(gState.unavailableReason);
        return;
    }
    if (cmd == nullptr || !EnsureInitialized(device, queue)) {
        gState.currentSlot = nullptr;
        return;
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

const FrameSnapshot& GetLatestSnapshot() { return gState.latest; }

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
