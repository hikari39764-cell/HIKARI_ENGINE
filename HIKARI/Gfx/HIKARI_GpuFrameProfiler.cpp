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
constexpr uint32_t kQueriesPerPass = 2u;
constexpr uint32_t kQueryCount = kPassCount * kQueriesPerPass;

struct FrameSlot {
    ComPtr<ID3D12QueryHeap> queryHeap{};
    ComPtr<ID3D12Resource> readbackBuffer{};
    std::array<uint8_t, kPassCount> used{};
    std::array<uint8_t, kPassCount> active{};
    std::array<Pass, kPassCount> parents{};
    std::array<Pass, kPassCount> activeStack{};
    uint32_t activeDepth = 0;
    bool resolved = false;
    uint64_t frameIndex = 0;
    uint64_t retireFence = 0;
};

struct ProfilerState {
    std::array<FrameSlot, kBufferedFrameCount> slots{};
    FrameSlot* currentSlot = nullptr;
    FrameSnapshot latest{};
    std::array<PERFORMANCE::RollingTimingWindow, kPassCount> rolling{};
    uint64_t timestampFrequency = 0;
    uint32_t writeSlotIndex = 0;
    uint32_t groupDepth = 0;
    bool initialized = false;
    bool enabled = true;
    const char* unavailableReason = "";
};

ProfilerState gState{};

uint32_t PassIndex(Pass pass) { return static_cast<uint32_t>(pass); }

uint32_t QueryIndex(Pass pass, uint32_t endpoint) { return PassIndex(pass) * kQueriesPerPass + endpoint; }

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

    const D3D12_RANGE readRange{0, sizeof(uint64_t) * kQueryCount};
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

    for (uint32_t i = 0; i < kPassCount; ++i) {
        if (slot.used[i] == 0) {
            continue;
        }

        const uint64_t start = queryData[QueryIndex(static_cast<Pass>(i), 0u)];
        const uint64_t end = queryData[QueryIndex(static_cast<Pass>(i), 1u)];
        if (end < start) {
            continue;
        }

        PassTiming& timing = snapshot.passes[i];
        timing.valid = true;
        timing.parentPass = slot.parents[i];
        timing.ticks = end - start;
        timing.gpuMs = static_cast<double>(timing.ticks) * 1000.0 / static_cast<double>(gState.timestampFrequency);
        auto& window = gState.rolling[i];
        window.Push(timing.gpuMs);
        timing.averageMs = window.Average();
        timing.p95Ms = window.p95;
        timing.p99Ms = window.p99;
        timing.maximumMs = window.maximum;
        timing.rollingSampleCount = static_cast<uint32_t>(window.count);
    }

    for (auto& timing : snapshot.passes)
        timing.exclusiveGpuMs = timing.gpuMs;
    for (const auto& timing : snapshot.passes) {
        if (timing.valid && IsValidPass(timing.parentPass))
            snapshot.passes[PassIndex(timing.parentPass)].exclusiveGpuMs -= timing.gpuMs;
    }
    for (auto& timing : snapshot.passes)
        timing.exclusiveGpuMs = (std::max)(0.0, timing.exclusiveGpuMs);

    const D3D12_RANGE writeRange{0, 0};
    slot.readbackBuffer->Unmap(0, &writeRange);
    slot.resolved = false;
    gState.latest = snapshot;
}

void ResetSlot(FrameSlot& slot, uint64_t frameIndex) {
    slot.used.fill(0u);
    slot.active.fill(0u);
    slot.parents.fill(Pass::Count);
    slot.activeDepth = 0;
    slot.frameIndex = frameIndex;
    slot.resolved = false;
}
} // namespace

void SetEnabled(bool enabled, const char* unavailableReason) {
    if (gState.enabled != enabled) {
        gState.rolling = {};
        // 無効化前の readback を新しい測定窓へ混ぜない。再利用自体は fence で保護する。
        for (auto& slot : gState.slots)
            slot.resolved = false;
    }
    gState.enabled = enabled;
    gState.unavailableReason = enabled ? "" : (unavailableReason != nullptr ? unavailableReason : "");
    if (!enabled) {
        gState.currentSlot = nullptr;
        ResetUnavailableSnapshot(gState.unavailableReason);
    }
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
    case Pass::TemporalMotionVectors: return "Temporal MotionVectors";
    case Pass::TemporalTaaResolve: return "Temporal TAA";
    case Pass::TemporalDlaaResolve: return "Temporal DLAA";
    case Pass::TemporalDlssResolve: return "Temporal DLSS";
    case Pass::ForwardOpaque: return "ForwardOpaque";
    case Pass::DepthAware: return "DepthAware";
    case Pass::ForwardTransparent: return "ForwardTransparent";
    case Pass::ParticleSimulation: return "Particle Simulation";
    case Pass::ParticleDraw: return "Particle Draw";
    case Pass::VolumetricLighting: return "Volumetric Lighting";
    case Pass::StylizedGeometryOutline: return "Stylized Geometry Outline";
    case Pass::StylizedVisibilityOverlay: return "Stylized Visibility Overlay";
    case Pass::StylizedScreenFeatures: return "Stylized Screen Features";
    case Pass::PostResolve: return "Post Resolve";
    case Pass::GameViewResolve: return "GameView Resolve";
    case Pass::SceneLayers: return "Scene Layers";
    case Pass::UiLayers: return "UI Layers";
    case Pass::ImGui: return "ImGui";
    case Pass::WaterVisibilityDepth: return "Water Visibility Depth";
    case Pass::StylizedClouds: return "Stylized Clouds";
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
    for (uint32_t i = 0; i < kPassCount; ++i) {
        if (slot->active[i] != 0) {
            const Pass pass = static_cast<Pass>(i);
            cmd->EndQuery(slot->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, QueryIndex(pass, 1u));
            slot->active[i] = 0;
        }
        if (slot->used[i] != 0) {
            const Pass pass = static_cast<Pass>(i);
            cmd->ResolveQueryData(slot->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, QueryIndex(pass, 0u),
                                  kQueriesPerPass, slot->readbackBuffer.Get(), sizeof(uint64_t) * QueryIndex(pass, 0u));
        }
    }

    slot->resolved = true;
    gState.currentSlot = nullptr;
}

void Shutdown() {
    for (FrameSlot& slot : gState.slots) {
        slot.queryHeap.Reset();
        slot.readbackBuffer.Reset();
        slot.used.fill(0u);
        slot.active.fill(0u);
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

    const uint32_t index = PassIndex(pass);
    if (slot->active[index] != 0 || slot->used[index] != 0 || slot->queryHeap == nullptr) {
        return false;
    }

    cmd->EndQuery(slot->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, QueryIndex(pass, 0u));
    slot->parents[index] = slot->activeDepth > 0u ? slot->activeStack[slot->activeDepth - 1u] : Pass::Count;
    slot->activeStack[slot->activeDepth++] = pass;
    slot->active[index] = 1u;
    slot->used[index] = 1u;
    return true;
}

void EndPass(ID3D12GraphicsCommandList* cmd, Pass pass) {
    FrameSlot* slot = gState.currentSlot;
    if (!gState.enabled || !gState.initialized || slot == nullptr || cmd == nullptr || !IsValidPass(pass)) {
        return;
    }

    const uint32_t index = PassIndex(pass);
    if (slot->active[index] == 0 || slot->queryHeap == nullptr) {
        return;
    }

    cmd->EndQuery(slot->queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, QueryIndex(pass, 1u));
    slot->active[index] = 0u;
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
