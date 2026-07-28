#pragma once

#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Core/MeshRenderer/Execution/HIKARI_MeshDrawExecutor.h"
#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererState.h"
#include "Render3D/GpuDriven/Backend/HIKARI_GeometryBackendPolicy.h"

namespace HIKARI::MESHRENDERER::INTERNAL {

    extern MeshRendererState gMeshRendererState;

    struct GeometryBackendExecutionResult {
        bool gpuBackendExecuted = false;
        RENDER3D::GPUDRIVEN::GeometryBackendKind executedGpuBackend =
            RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs;
    };

#if defined(HIKARI_WITH_EDITOR)
    struct MeshFrameBindingOverrides;
#endif

    bool EnsureInitialized(const GFX::Context& graphics);
    MeshFrameResources& GetActiveMeshFrameResources();
    MATH::Mat4 ResolveGpuDrivenCullingViewProj();
    MATH::Vec3 ResolveGpuDrivenCullingCameraPosition();
    const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& GetSceneSourcePass(
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind);
    bool HasGpuDrivenPassSource(
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind);
    bool HasGpuDrivenSceneSource();
    bool BeginFrameInternal(
        const GFX::Context& graphics,
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t screenWidth,
        uint32_t screenHeight,
        RenderDebugView debugView,
        const MeshFrameCameraOverrides* cameraOverrides);
    void CommitActiveMaterialDataFrame(
        ID3D12GraphicsCommandList* commandList);
    void SyncSurfaceGpuSceneMaterialFrame(const GFX::Context& graphics);
    void UploadGpuDrivenSceneFrame(const GFX::Context& graphics);
    void UpdateTraditionalCommandStreamStats();
    uint32_t MakePreDepthGpuDrivenPassMask();
    uint32_t MakeMainCameraGpuDrivenPassMask();
    void BuildStrictGpuDrivenCommandFrame(const GFX::Context& graphics);
    void ResetGpuDrivenFrameState(const GFX::Context& graphics);
    void PrepareGpuDrivenFrameState(const GFX::Context& graphics);
    void BuildGpuDrivenWorkFrame(
        const GFX::Context& graphics,
        const HIKARI::RENDER3D::DEPTH::DepthPyramidView* depthPyramid = nullptr,
        uint32_t passMask = 0xffffffffu,
        bool collectCounterReadback = true);
    void UpdateMeshletBackendDebugStats();
    void SyncGpuDrivenBackendAvailability();
    void UpdateGpuDrivenCommandStreamDebugStats();
    bool IsGpuDrivenWorkPreparedForPass(
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind);
    MeshDrawContext BuildDrawContext(
        const GFX::Context& graphics,
        bool depthAwarePhase,
        MeshDrawPassKind passKind,
        const MeshPassResources& passResources
#if defined(HIKARI_WITH_EDITOR)
        , const MeshFrameBindingOverrides* overrides = nullptr
#endif
    );
    ID3D12PipelineState* ResolveTraditionalStaticPso(
        const GFX::Context& graphics,
        MeshDrawPassKind passKind,
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
        const VFX::VariantKey& variant);
    bool ExecuteTraditionalDrawFrame(
        const GFX::Context& graphics,
        const MeshPassResources& passResources,
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass,
        MeshDrawPassKind passKind
#if defined(HIKARI_WITH_EDITOR)
        ,
        RENDER3D::GPUDRIVEN::GpuDrivenLayer* layerOverride = nullptr,
        const MeshFrameBindingOverrides* bindingOverrides = nullptr
#endif
    );
    GeometryBackendExecutionResult ExecuteGeometryBackendPlan(
        const GFX::Context& graphics,
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass,
        const MeshPassResources& passResources,
        MeshDrawPassKind passKind,
        RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind);
    GeometryBackendExecutionResult ExecuteDepthVisibilityBackendPlan(
        const GFX::Context& graphics,
        RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass,
        const MeshPassResources& passResources);

#if defined(HIKARI_WITH_EDITOR)
    struct MeshFrameBindingOverrides {
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS cullingCameraAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS lightAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS shadowAddress = 0;
        D3D12_GPU_VIRTUAL_ADDRESS skyEnvironmentAddress = 0;
        RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBuffer*
            surfaceGpuSceneFrameBuffer = nullptr;
        RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamBuffer*
            traditionalCommandStreamBuffer = nullptr;
    };

    void ResetEditorInteractiveResources();
    bool EnsureEditorInteractiveResources();
#endif

} // namespace HIKARI::MESHRENDERER::INTERNAL
