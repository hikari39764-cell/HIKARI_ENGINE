#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>

namespace HIKARI::GFX::GPU_PROFILE {

    enum class Pass : uint32_t {
        ShadowMap = 0,
        GeometryAux,
        ClusterCull,
        DepthPrepass,
        TraditionalDrawGeometryAux,
        TraditionalDrawForward,
        MeshletDrawGeometryAux,
        MeshletDrawDepthPrepass,
        MeshletDrawForward,
        MeshletDrawDepthAware,
        MeshletDrawTransparent,
        MeshletDrawGeometryOutline,
        MeshletDrawVisibilityOverlay,
        TraditionalDrawShadow,
        MeshletDrawShadow,
        TraditionalDrawShadowStatic,
        MeshletDrawShadowStatic,
        TraditionalDrawShadowDynamic,
        MeshletDrawShadowDynamic,
        TraditionalDrawShadowFallback,
        MeshletDrawShadowFallback,
        SsaoMain,
        SsaoBlur,
        ScreenSpaceReflections,
        TemporalMotionVectors,
        TemporalTaaResolve,
        TemporalDlaaResolve,
        TemporalDlssResolve,
        ForwardOpaque,
        DepthAware,
        ForwardTransparent,
        ParticleSimulation,
        ParticleDraw,
        VolumetricLighting,
        StylizedGeometryOutline,
        StylizedVisibilityOverlay,
        StylizedScreenFeatures,
        PostResolve,
        GameViewResolve,
        SceneLayers,
        UiLayers,
        ImGui,
        WaterVisibilityDepth,
        StylizedClouds,
        SceneCaptures,
        CommandFrame,
        Count
    };

    struct PassTiming {
        const char* name = "";
        bool valid = false;
        double gpuMs = 0.0;
        uint64_t ticks = 0;
    };

    struct FrameSnapshot {
        bool initialized = false;
        bool profilerEnabled = true;
        bool gpuTimingAvailable = false;
        uint64_t frameIndex = 0;
        uint64_t timestampFrequency = 0;
        const char* unavailableReason = "";
        std::array<PassTiming, static_cast<size_t>(Pass::Count)> passes{};
    };

    const char* ToString(Pass pass);
    void SetEnabled(bool enabled, const char* unavailableReason = "");
    bool IsEnabled();

    void BeginFrame(
        ID3D12Device* device,
        ID3D12CommandQueue* queue,
        ID3D12GraphicsCommandList* cmd,
        uint64_t frameIndex, uint64_t completedFence, uint64_t retireFence);
    void EndFrame(ID3D12GraphicsCommandList* cmd);
    void Shutdown();

    bool BeginPass(ID3D12GraphicsCommandList* cmd, Pass pass);
    void EndPass(ID3D12GraphicsCommandList* cmd, Pass pass);

    const FrameSnapshot& GetLatestSnapshot();

    class ScopedGpuTimer {
    public:
        ScopedGpuTimer(ID3D12GraphicsCommandList* cmd, Pass pass);
        ~ScopedGpuTimer();

        ScopedGpuTimer(const ScopedGpuTimer&) = delete;
        ScopedGpuTimer& operator=(const ScopedGpuTimer&) = delete;

    private:
        ID3D12GraphicsCommandList* cmd_ = nullptr;
        Pass pass_ = Pass::Count;
        bool active_ = false;
    };

    class ScopedGpuTimerGroup final {
    public:
        ScopedGpuTimerGroup(ID3D12GraphicsCommandList* cmd, Pass pass);
        ~ScopedGpuTimerGroup();
        ScopedGpuTimerGroup(const ScopedGpuTimerGroup&) = delete;
        ScopedGpuTimerGroup& operator=(const ScopedGpuTimerGroup&) = delete;
    private:
        ScopedGpuTimer timer_;
    };

} // namespace HIKARI::GFX::GPU_PROFILE
