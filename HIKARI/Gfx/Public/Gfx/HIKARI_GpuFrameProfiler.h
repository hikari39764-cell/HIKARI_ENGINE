#pragma once

#include <cstdint>

#include <d3d12.h>

namespace HIKARI::GFX::GPU_PROFILE {

// GPU の計時は登録表（Diagnostics/HIKARI_MetricRegistry.h）へ出す。
// frame.gpuMs（コマンド全体）と gpu.<pass 名>.ms|calls|selfMs。結果は読み戻しのため数フレーム遅れて届く。

// 1 フレームで計れる実行回数の上限。同じ pass も実行のたびに 1 組ずつ使う。固定長なのでフレームごとの確保はない。
inline constexpr uint32_t kMaxExecutionsPerFrame = 512u;

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
    SubsurfaceScattering,
    RayTracedShadow,
    PathTracedReference,
    TemporalMotionVectors,
    TemporalMasks,
    TemporalStableDepth,
    TemporalTaaResolve,
    TemporalDlaaResolve,
    TemporalDlssResolve,
    ForwardOpaque,
    DepthAware,
    ForwardTransparent,
    DepthOfField,
    MotionBlur,
    ParticleSimulation,
    ParticleDraw,
    FluidSimulation,
    FluidRender,
    VolumetricLighting,
    StylizedGeometryOutline,
    StylizedVisibilityOverlay,
    StylizedScreenFeatures,
    PostSceneResolve,
    PostPreTemporal,
    PostHdr,
    PostBloom,
    PresentationComposition,
    GameViewResolve,
    SceneLayers,
    UiLayers,
    ImGui,
    WaterVisibilityDepth,
    VolumetricClouds,
    SceneCaptures,
    CommandFrame,
    EnvironmentLighting,
    DynamicDiffuseGi,
    CloudSolarVisibility,
    RuntimeRayScenePreparation,
    // 前向き不透明の Meshlet Draw Forward の中を、描く PSO の種類で分けた入れ子の区間。
    MeshletForwardStandard,
    MeshletForwardMaterialProgram,
    MeshletForwardTerrain,
    GameUi,
    // 流体の解算（Fluid Simulation）と描画（Fluid Render）の中を工程で分けた入れ子の区間。解算は子步ごとに計る。
    FluidSolveActivity,
    FluidSolveAdvect,
    FluidSolveForces,
    FluidSolvePressure,
    FluidRenderLight,
    FluidRenderCameraVolume,
    FluidRenderMarch,
    Count
};

const char* ToString(Pass pass);
// pass の所属モジュール（Render3D、Vfx、Post など）。pass の表の同じ行で宣言している。
const char* ModuleOf(Pass pass);
void SetEnabled(bool enabled, const char* unavailableReason = "");
bool IsEnabled();

void BeginFrame(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* cmd, uint64_t frameIndex,
                uint64_t completedFence, uint64_t retireFence);
void EndFrame(ID3D12GraphicsCommandList* cmd);
void Shutdown();
void ResetStatistics();

bool BeginPass(ID3D12GraphicsCommandList* cmd, Pass pass);
void EndPass(ID3D12GraphicsCommandList* cmd, Pass pass);

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
