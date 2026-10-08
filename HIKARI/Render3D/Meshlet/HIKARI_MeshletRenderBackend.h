#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <future>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Render3D/Meshlet/HIKARI_MeshletVisibility.h"
#include "Render3D/GpuDriven/Backend/HIKARI_GeometryBackendContext.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenCommandBucket.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

namespace HIKARI::RENDER3D::MESHLET {

constexpr size_t kForwardOpaquePipelineVariantCount =
    static_cast<size_t>(ForwardShadingCostMode::NoMaterialExtras) + 1u;

enum class MeshletPipelineKind : uint32_t {
    ForwardOpaque,
    ForwardDepthAware,
    ForwardTransparent,
    Shadow,
    GeometryAux,
    GeometryAuxFeatureMask,
    GeometryOutline,
    VisibilityOverlay,
    DepthPrepass,
    WaterVisibility,
};

namespace MeshletPipelineMask {
constexpr uint32_t ForwardOpaque = 1u << 0;
constexpr uint32_t ForwardDepthAware = 1u << 1;
constexpr uint32_t ForwardTransparent = 1u << 2;
constexpr uint32_t Shadow = 1u << 3;
constexpr uint32_t GeometryAux = 1u << 4;
constexpr uint32_t DepthPrepass = 1u << 5;
constexpr uint32_t GeometryOutline = 1u << 6;
constexpr uint32_t VisibilityOverlay = 1u << 7;
// Auxiliary producer only.  It is requested with the main renderer
// but deliberately excluded from global readiness so missing water
// shaders/PSOs preserve the core mesh-shader route and opaque-depth
// fallback.
constexpr uint32_t WaterVisibility = 1u << 8;
constexpr uint32_t GeometryAuxFeatureMask = 1u << 9;
constexpr uint32_t MainRenderer = ForwardOpaque | ForwardDepthAware | ForwardTransparent | GeometryAux |
                                  GeometryAuxFeatureMask | GeometryOutline | VisibilityOverlay | DepthPrepass |
                                  WaterVisibility;
constexpr uint32_t ShadowRenderer = Shadow;
constexpr uint32_t All = MainRenderer | ShadowRenderer;
} // namespace MeshletPipelineMask

struct MeshletRenderBackendStats {
    MeshletVisibilityStats visibility{};
    bool initialized = false;
    bool shaderModel65Supported = false;
    bool meshShaderSupported = false;
    bool meshShaderPipelineStatsSupported = false;
    bool shaderCompileReady = false;
    bool dispatchArgumentBufferReady = false;
    bool dispatchCommandSignatureReady = false;
    bool forwardPipelineReady = false;
    bool depthAwarePipelineReady = false;
    bool transparentPipelineReady = false;
    bool shadowPipelineReady = false;
    bool geometryAuxPipelineReady = false;
    bool geometryFeatureMaskPipelineReady = false;
    bool geometryOutlinePipelineReady = false;
    bool visibilityOverlayPipelineReady = false;
    bool depthPrepassPipelineReady = false;
    bool waterVisibilityPipelineReady = false;
    bool pipelineReady = false;
    uint32_t meshShaderTier = 0;
    size_t pipelineCreateRequestCount = 0;
    size_t pipelineCreateReadyCount = 0;
    size_t preparedForwardVariantCount = 0;
    size_t failedForwardVariantCount = 0;
    size_t requestedDispatchCount = 0;
    size_t submittedDispatchCount = 0;
    size_t skippedDispatchCount = 0;
    size_t submitCallCount = 0;
    size_t skippedBucketCount = 0;
    size_t forwardSubmittedDispatchCount = 0;
    size_t geometryAuxSubmittedDispatchCount = 0;
    size_t geometryOutlineSubmittedDispatchCount = 0;
    size_t visibilityOverlaySubmittedDispatchCount = 0;
    size_t depthPrepassSubmittedDispatchCount = 0;
    size_t shadowSubmittedDispatchCount = 0;
    size_t backFaceSubmitCallCount = 0;
    size_t doubleSidedSubmitCallCount = 0;
};

struct MeshletRenderExecutionContext {
    ID3D12GraphicsCommandList* commandList = nullptr;
    GPUDRIVEN::GpuDrivenPassKind pass = GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque;
    const GPUDRIVEN::GpuVisibilityResult* visibility = nullptr;
    const GPUDRIVEN::GpuDrivenDrawCommandRange* drawCommandRange = nullptr;
    MeshletPipelineKind pipelineKind = MeshletPipelineKind::ForwardOpaque;
    GFX::GPU_PROFILE::Pass profilePass = GFX::GPU_PROFILE::Pass::Count;
    bool reflectionCapture = false;
    bool reflectionMaterialCapture = false;
    ForwardShadingCostMode forwardCostMode = ForwardShadingCostMode::Full;
    // Forward/GeometryAux は一般面と地形を別 PSO で描く。地形が無ければ地形 dispatch を省く。
    bool terrainSurfacesPresent = false;
    // Forward は標準 PBR の面と材質 program の面を別 PSO で描く。program の面が無ければその dispatch を省く。
    bool materialProgramSurfacesPresent = false;
    // debug view 中の Forward は全経路 PSO で描き、分割前と同じ表示を保つ。
    bool forwardDebugView = false;
};

class MeshletRenderBackend final {
public:
    using PipelineBucketArray =
        std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, GPUDRIVEN::kGpuDrivenCommandBucketCount>;
    using ForwardPipelineVariantArray = std::array<PipelineBucketArray, kForwardOpaquePipelineVariantCount>;

    bool Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature);
    bool Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature, uint32_t pipelineMask);
    // SSR capture PSO が使えるか。PSO は Initialize が別スレッドで作り、完成前と失敗時は false。
    bool PrepareReflectionCapture(bool captureMaterial = false);
    // 診断 variant は最初の要求時にだけ準備する。失敗は Reset まで再試行しない。
    bool PrepareForwardVariant(ForwardShadingCostMode mode);
    // debug view 用の全経路 Forward PSO。最初の要求時にだけ作る。
    bool PrepareForwardDebugPipelines();
    void Reset();
    void ResetFrame();
    void ResetVisibility() {
        visibility_.Reset();
        stats_.visibility = {};
    }
    bool Execute(const MeshletRenderExecutionContext& ctx);
    MeshletVisibilityBinding BeginVisibility(const GFX::Context& graphics, const DEPTH::DepthPyramidView* depth,
                                             uint32_t phase) {
        auto binding = visibility_.Begin(graphics, depth, phase);
        stats_.visibility = visibility_.GetStats();
        return binding;
    }
    void EndVisibility(const GFX::Context& graphics) {
        visibility_.End(graphics);
        stats_.visibility = visibility_.GetStats();
    }

    const MeshletRenderBackendStats& GetStats() const;
    // Query only: 未作成の診断 variant は nullptr。通常描画への fallback は Execute が決める。
    ID3D12PipelineState* GetPipelineState(MeshletPipelineKind kind, GPUDRIVEN::GpuDrivenCommandBucket bucket,
                                          ForwardShadingCostMode costMode = ForwardShadingCostMode::Full) const;

private:
    // 地形専用 PSO を持つ kind だけ非 null。
    ID3D12PipelineState* GetTerrainPipelineState(MeshletPipelineKind kind,
                                                 GPUDRIVEN::GpuDrivenCommandBucket bucket) const;
    // 分割した kind は分割したすべての PSO（一般面・材質 program・地形）が揃って初めて描ける。
    void ApplySurfacePartitionReadiness();
    bool AreReflectionPipelinesReady(bool captureMaterial) const;

    // SSR capture の PSO。[captureMaterial] ごとに標準 PBR の面と材質 program の面を別 PSO で描く。
    struct ReflectionCapturePipelines {
        std::array<PipelineBucketArray, 2> standard{};
        std::array<PipelineBucketArray, 2> program{};
    };

    MeshletVisibility visibility_{};
    Microsoft::WRL::ComPtr<ID3D12Device> device_{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_{};
    ReflectionCapturePipelines reflectionPipelines_{};
    std::future<ReflectionCapturePipelines> reflectionCaptureTask_{};
    // [0] は標準 PBR の面だけを描く通常 PSO。診断 variant は一般面すべてを描く。
    ForwardPipelineVariantArray forwardPipelineStates_{};
    PipelineBucketArray programForwardPipelineStates_{};
    std::array<bool, kForwardOpaquePipelineVariantCount> forwardVariantAttempted_{};
    PipelineBucketArray depthAwarePipelineStates_{};
    PipelineBucketArray transparentPipelineStates_{};
    PipelineBucketArray shadowPipelineStates_{};
    PipelineBucketArray geometryAuxPipelineStates_{};
    PipelineBucketArray geometryFeatureMaskPipelineStates_{};
    PipelineBucketArray geometryOutlinePipelineStates_{};
    PipelineBucketArray visibilityOverlayPipelineStates_{};
    PipelineBucketArray depthPrepassPipelineStates_{};
    PipelineBucketArray waterVisibilityPipelineStates_{};
    PipelineBucketArray terrainForwardPipelineStates_{};
    PipelineBucketArray terrainGeometryAuxPipelineStates_{};
    PipelineBucketArray terrainGeometryFeatureMaskPipelineStates_{};
    PipelineBucketArray forwardDebugPipelineStates_{};
    bool forwardDebugAttempted_ = false;
    uint32_t pipelineMask_ = MeshletPipelineMask::All;
    MeshletRenderBackendStats stats_{};
};

} // namespace HIKARI::RENDER3D::MESHLET
