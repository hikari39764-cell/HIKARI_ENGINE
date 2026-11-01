#pragma once

#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Core/MeshRenderer/Execution/HIKARI_MeshDrawExecutor.h"
#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererState.h"
#include "Render3D/Debug/HIKARI_RenderDebugView.h"
#include "Render3D/GpuDriven/Backend/HIKARI_GeometryBackendPolicy.h"

namespace HIKARI::MESHRENDERER::INTERNAL {

extern MeshRendererState gMeshRendererState;

struct GeometryBackendExecutionResult {
    bool gpuBackendExecuted = false;
    bool meshShaderExecuted = false;
    bool traditionalIndirectExecuted = false;
    RENDER3D::GPUDRIVEN::GeometryBackendKind executedGpuBackend =
        RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs;

    void RecordExecutedBackend(RENDER3D::GPUDRIVEN::GeometryBackendKind backend) noexcept {
        gpuBackendExecuted = true;
        executedGpuBackend = backend;
        meshShaderExecuted =
            meshShaderExecuted || backend == RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader;
        traditionalIndirectExecuted = traditionalIndirectExecuted ||
                                      backend == RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenTraditionalVsPs;
    }
};

struct MeshFrameBindingOverrides;

bool EnsureInitialized(const GFX::Context& graphics);
void RecordOceanSpectrumForWaterFrame(const GFX::Context& graphics,
                                      const RENDER3D::WATER::WaterSurfaceGpuFrameBuffer& waterSurfaceBuffer);
void ResetOceanSpectrumLazyInitializationState() noexcept;
MeshFrameResources& GetActiveMeshFrameResources();
MATH::Mat4 ResolveGpuDrivenCullingViewProj();
MATH::Vec3 ResolveGpuDrivenCullingCameraPosition();
const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& GetSceneSourcePass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind);
bool HasGpuDrivenPassSource(RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind);
bool HasGpuDrivenSceneSource();
bool BeginFrameInternal(const GFX::Context& graphics, const Camera3D& camera, const SceneEnvironment& environment,
                        const RENDER3D::LIGHTING::SceneLightingSet& lighting, uint32_t screenWidth,
                        uint32_t screenHeight, RenderDebugView debugView,
                        const MeshFrameCameraOverrides* cameraOverrides);
bool CommitActiveMaterialDataFrame(ID3D12GraphicsCommandList* commandList);
bool ExecutePreparedParticleForwardTransparent(const GFX::Context& graphics, D3D12_GPU_VIRTUAL_ADDRESS cameraAddress,
                                               const MeshPassResources& passResources);
bool ExecuteParticleForwardTransparent(RENDER3D::PARTICLES::ParticleGpuDrivenPass& particlePass,
                                       const GFX::Context& graphics, D3D12_GPU_VIRTUAL_ADDRESS cameraAddress,
                                       const MATH::Vec3& cameraPosition, const MeshPassResources& passResources);
void SyncSurfaceGpuSceneMaterialFrame(const GFX::Context& graphics);
void SyncGpuDrivenSceneMaterialSources(const GFX::Context& graphics, uintptr_t sourceIdentity,
                                       const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source);
void UploadGpuDrivenSceneFrame(const GFX::Context& graphics);
void UpdateTraditionalCommandStreamStats();
uint32_t MakePreDepthGpuDrivenPassMask();
uint32_t MakeMainCameraGpuDrivenPassMask();
void BuildStrictGpuDrivenCommandFrame(const GFX::Context& graphics);
void ResetGpuDrivenFrameState(const GFX::Context& graphics);
void PrepareGpuDrivenFrameState(const GFX::Context& graphics);
void BuildGpuDrivenWorkFrame(const GFX::Context& graphics,
                             const HIKARI::RENDER3D::DEPTH::DepthPyramidView* depthPyramid = nullptr,
                             uint32_t passMask = 0xffffffffu, bool collectCounterReadback = true);
void UpdateMeshletBackendDebugStats();
void SyncGpuDrivenBackendAvailability();
void UpdateGpuDrivenCommandStreamDebugStats();
bool IsGpuDrivenWorkPreparedForPass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind passKind);
MeshDrawContext BuildDrawContext(const GFX::Context& graphics, bool depthAwarePhase, MeshDrawPassKind passKind,
                                 const MeshPassResources& passResources,
                                 const MeshFrameBindingOverrides* overrides = nullptr);
bool ExecuteTraditionalDrawFrame(const GFX::Context& graphics, const MeshPassResources& passResources,
                                 RENDER3D::GPUDRIVEN::GpuDrivenPassKind gpuPass, MeshDrawPassKind passKind,
                                 RENDER3D::GPUDRIVEN::GpuDrivenLayer* layerOverride = nullptr,
                                 const MeshFrameBindingOverrides* bindingOverrides = nullptr);
bool PrepareForwardReflectionCapturePipelines(const GFX::Context& graphics, bool captureSubsurface);

GeometryBackendExecutionResult ExecuteGeometryBackendPlan(const GFX::Context& graphics,
                                                          RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass,
                                                          const MeshPassResources& passResources,
                                                          MeshDrawPassKind passKind,
                                                          RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind);
struct MeshFrameBindingOverrides;
GeometryBackendExecutionResult ExecuteDepthVisibilityBackendPlan(
    const GFX::Context& graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass, const MeshPassResources& passResources,
    const MeshFrameBindingOverrides* bindingOverrides = nullptr);

struct MeshFrameBindingOverrides {
    D3D12_GPU_VIRTUAL_ADDRESS cameraAddress = 0;
    D3D12_GPU_VIRTUAL_ADDRESS cullingCameraAddress = 0;
    D3D12_GPU_VIRTUAL_ADDRESS lightAddress = 0;
    D3D12_GPU_VIRTUAL_ADDRESS localLightsAddress = 0;
    D3D12_GPU_VIRTUAL_ADDRESS localLightClusterHeadersAddress = 0;
    D3D12_GPU_VIRTUAL_ADDRESS localLightClusterIndicesAddress = 0;
    D3D12_GPU_VIRTUAL_ADDRESS shadowAddress = 0;
    D3D12_GPU_VIRTUAL_ADDRESS skyEnvironmentAddress = 0;
    D3D12_GPU_VIRTUAL_ADDRESS jointPaletteAddress = 0;
    RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBuffer* surfaceGpuSceneFrameBuffer = nullptr;
    RENDER3D::WATER::WaterSurfaceGpuFrameBuffer* waterSurfaceGpuFrameBuffer = nullptr;
    RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamBuffer* traditionalCommandStreamBuffer = nullptr;
};

GeometryBackendExecutionResult ExecuteSecondaryViewGeometryBackendPlan(
    const GFX::Context& graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass, const MeshPassResources& passResources,
    MeshDrawPassKind passKind, RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind,
    RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer, const MeshFrameBindingOverrides& bindingOverrides,
    bool allowCompatibilityFallback = false);

#if defined(HIKARI_WITH_EDITOR)
GeometryBackendExecutionResult ExecuteEditorInteractiveGeometryBackendPlan(
    const GFX::Context& graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind pass, const MeshPassResources& passResources,
    MeshDrawPassKind passKind, RENDER3D::MESHLET::MeshletPipelineKind meshletPipelineKind,
    RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer, const MeshFrameBindingOverrides& bindingOverrides);

bool ExecuteEditorInteractiveStylizedSurfacePass(const GFX::Context& graphics, uint32_t screenWidth,
                                                 uint32_t screenHeight, const EditorInteractiveRenderSettings& settings,
                                                 const MeshPassResources& passResources,
                                                 RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer,
                                                 const MeshFrameBindingOverrides& bindingOverrides);
bool ExecuteEditorInteractiveStylizedGeometryPasses(const GFX::Context& graphics,
                                                    const EditorInteractiveRenderSettings& settings,
                                                    const MeshPassResources& passResources,
                                                    RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer,
                                                    const MeshFrameBindingOverrides& bindingOverrides);

void ResetEditorInteractiveResources();
bool EnsureEditorInteractiveResources();
#endif

} // namespace HIKARI::MESHRENDERER::INTERNAL
