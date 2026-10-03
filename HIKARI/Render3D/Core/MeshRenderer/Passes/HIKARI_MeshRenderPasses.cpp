#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/MeshRenderer/Bindings/HIKARI_MeshResourceBindings.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"

#include <algorithm>
#include <array>

#include "Gfx/HIKARI_PixProfiler.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Render3D/GpuDriven/HIKARI_GpuDepthVisibilityLayer.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceBuffer.h"

namespace HIKARI::MESHRENDERER::INTERNAL {
namespace {
constexpr std::array kMaterialFeaturePasses{
    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque,
    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware,
    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent,
};

bool ExecuteMaterialScopedFeaturePasses(const GFX::Context& graphics, const MeshPassResources& passResources,
                                        MeshDrawPassKind drawPass,
                                        RENDER3D::MESHLET::MeshletPipelineKind meshletPipeline) {
    bool hasSource = false;
    bool allSourcesExecuted = true;
    SyncGpuDrivenBackendAvailability();
    for (const RENDER3D::GPUDRIVEN::GpuDrivenPassKind sourcePass : kMaterialFeaturePasses) {
        if (!HasGpuDrivenPassSource(sourcePass) ||
            gMeshRendererState.gpuDrivenLayer.GetPassExecutionPlan(sourcePass).gpuBackendCount == 0u) {
            continue;
        }
        hasSource = true;
        allSourcesExecuted &= ExecuteGeometryBackendPlan(graphics, sourcePass, passResources, drawPass, meshletPipeline)
                                  .gpuBackendExecuted;
    }
    return hasSource && allSourcesExecuted;
}
} // namespace

bool RenderGeometryAuxPassInternal(const GFX::Context& graphics, RENDER3D::SURFACE::SceneSurfaceBuffer& surfaceBuffer,
                                   D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv, bool writeFeatureMask) {
    const bool hasMeshes = HasGpuDrivenPassSource(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);
    auto& sprites = gMeshRendererState.worldSpritePass;
    if (!hasMeshes && !sprites.HasOpaqueWork()) {
        return false;
    }

    const CameraCB* camera = GetCameraConstants();
    const uint32_t width = static_cast<uint32_t>((std::max)(1.0f, camera ? camera->screenParams.x : 1.0f));
    const uint32_t height = static_cast<uint32_t>((std::max)(1.0f, camera ? camera->screenParams.y : 1.0f));
    if (!surfaceBuffer.EnsureSize(graphics, width, height, writeFeatureMask)) {
        return false;
    }

    if (sceneDsv.ptr == 0) {
        return false;
    }

    GFX::PIX::ScopedGpuEvent pixGeometry(graphics.cmdList, GFX::PIX::kColorRender, "SceneSurface");
    surfaceBuffer.BeginWrite(graphics.cmdList, sceneDsv);
    MeshPassResources passResources{};
    const bool meshesWritten =
        !hasMeshes || ExecuteGeometryBackendPlan(
                          graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind::GeometryAux, passResources,
                          writeFeatureMask ? MeshDrawPassKind::GeometryAuxFeatureMask : MeshDrawPassKind::GeometryAux,
                          writeFeatureMask ? RENDER3D::MESHLET::MeshletPipelineKind::GeometryAuxFeatureMask
                                           : RENDER3D::MESHLET::MeshletPipelineKind::GeometryAux)
                          .gpuBackendExecuted;
    const bool spritesWritten =
        !sprites.HasOpaqueWork() ||
        sprites.ExecuteSurface(graphics, GetActiveMeshFrameResources().view.Bindings().camera, writeFeatureMask);
    surfaceBuffer.EndWrite(graphics.cmdList);
    return meshesWritten && spritesWritten;
}

bool RenderDepthPrepassInternal(const GFX::Context& graphics, D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv,
                                bool useCullingCamera) {
    const bool hasMeshes = HasGpuDrivenPassSource(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass);
    auto& sprites = gMeshRendererState.worldSpritePass;
    if (!hasMeshes && !sprites.HasOpaqueWork()) {
        return false;
    }

    ID3D12GraphicsCommandList* cmd = graphics.cmdList;
    if (cmd == nullptr || sceneDsv.ptr == 0) {
        return false;
    }

    const CameraCB* camera = useCullingCamera ? GetGpuDrivenCullingCameraConstants() : GetCameraConstants();
    const uint32_t width = static_cast<uint32_t>((std::max)(1.0f, camera ? camera->screenParams.x : 1.0f));
    const uint32_t height = static_cast<uint32_t>((std::max)(1.0f, camera ? camera->screenParams.y : 1.0f));
    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};

    GFX::PIX::ScopedGpuEvent pixDepth(cmd, GFX::PIX::kColorRender, "GpuDepthVisibility.DepthPrepass");
    cmd->OMSetRenderTargets(0, nullptr, FALSE, &sceneDsv);
    cmd->RSSetViewports(1, &viewport);
    cmd->RSSetScissorRects(1, &scissor);

    MeshFrameBindingOverrides overrides{};
    if (useCullingCamera) {
        overrides.cameraAddress = GetActiveMeshFrameResources().view.Bindings().cullingCamera;
        if (overrides.cameraAddress == 0u)
            return false;
    }
    const MeshPassResources passResources{};
    const bool meshesWritten =
        !hasMeshes || ExecuteDepthVisibilityBackendPlan(graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass,
                                                        passResources, useCullingCamera ? &overrides : nullptr)
                          .gpuBackendExecuted;
    const auto cameraAddress =
        useCullingCamera ? overrides.cameraAddress : GetActiveMeshFrameResources().view.Bindings().camera;
    const bool spritesWritten = !sprites.HasOpaqueWork() || sprites.ExecuteDepth(graphics, cameraAddress);
    return meshesWritten && spritesWritten;
}

bool RenderWaterVisibilityDepthInternal(const GFX::Context& graphics, D3D12_CPU_DESCRIPTOR_HANDLE waterDsv,
                                        const MeshPassResources& passResources) {
    if (!HasGpuDrivenPassSource(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware) ||
        graphics.cmdList == nullptr || waterDsv.ptr == 0 || !passResources.sceneDepth.IsSampleable()) {
        return false;
    }

    const CameraCB* camera = GetCameraConstants();
    const uint32_t width = static_cast<uint32_t>(
        (std::max)(1.0f, camera ? camera->screenParams.x : static_cast<float>(passResources.sceneDepth.width)));
    const uint32_t height = static_cast<uint32_t>(
        (std::max)(1.0f, camera ? camera->screenParams.y : static_cast<float>(passResources.sceneDepth.height)));
    if (width != passResources.sceneDepth.width || height != passResources.sceneDepth.height) {
        return false;
    }

    D3D12_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};

    GFX::PIX::ScopedGpuEvent pixWater(graphics.cmdList, GFX::PIX::kColorRender, "GpuDepthVisibility.Water");
    graphics.cmdList->OMSetRenderTargets(0, nullptr, FALSE, &waterDsv);
    graphics.cmdList->RSSetViewports(1, &viewport);
    graphics.cmdList->RSSetScissorRects(1, &scissor);

    const GeometryBackendExecutionResult backendResult = ExecuteGeometryBackendPlan(
        graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware, passResources,
        MeshDrawPassKind::WaterVisibility, RENDER3D::MESHLET::MeshletPipelineKind::WaterVisibility);
    return backendResult.gpuBackendExecuted;
}

RENDER3D::SurfaceLightingBindings ResolveSurfaceLightingBindings() {
    const auto& frame = GetActiveMeshFrameResources();
    const auto& view = frame.view.Bindings();
    RENDER3D::SurfaceLightingBindings lighting{};
    lighting.light = view.light;
    lighting.shadow = view.shadow;
    lighting.skyEnvironment = view.skyEnvironment;
    lighting.localLights = view.localLights.lights;
    lighting.localLightHeaders = view.localLights.clusterHeaders;
    lighting.localLightIndices = view.localLights.clusterIndices;
    lighting.shadowMap = SHADOW::GetDirectionalShadowSrv();
    lighting.irradiance = ResolveIblIrradianceSrv(gMeshRendererState.fallbackTextureHandle);
    lighting.prefiltered = ResolveIblPrefilteredSrv(gMeshRendererState.fallbackTextureHandle);
    return lighting;
}

bool ExecuteParticleForwardTransparent(RENDER3D::PARTICLES::ParticleGpuDrivenPass& particlePass,
                                       const GFX::Context& graphics, D3D12_GPU_VIRTUAL_ADDRESS cameraAddress,
                                       const MATH::Vec3& cameraPosition, const MeshPassResources& passResources) {
    if (!particlePass.HasDrawWork()) {
        return true;
    }
    const MeshFrameResources& frame = GetActiveMeshFrameResources();
    RENDER3D::PARTICLES::ParticleGpuDrivenDrawBindings bindings{};
    bindings.camera = cameraAddress;
    static_cast<RENDER3D::SurfaceLightingBindings&>(bindings) = ResolveSurfaceLightingBindings();
    bindings.materialData = frame.materialDataBuffer != nullptr ? frame.materialDataBuffer->GetGPUVirtualAddress() : 0u;
    bindings.materialParameters = frame.materialProgramParameterBuffer != nullptr
                                      ? frame.materialProgramParameterBuffer->GetGPUVirtualAddress()
                                      : 0u;
    bindings.materialResources = frame.materialProgramResourceBuffer != nullptr
                                     ? frame.materialProgramResourceBuffer->GetGPUVirtualAddress()
                                     : 0u;
    if (passResources.sceneDepth.IsSampleable()) {
        bindings.sceneDepth = passResources.sceneDepth.srv;
    }
    bindings.cameraPosition = cameraPosition;
    bindings.cameraPositionValid = true;
    bindings.compositionTarget = passResources.compositionTarget;
    bindings.compositionDepth = passResources.compositionDepth;
    bindings.width = passResources.sceneDepth.width;
    bindings.height = passResources.sceneDepth.height;
    return particlePass.ExecuteForwardTransparent(graphics, bindings);
}

bool ExecutePreparedParticleForwardTransparent(const GFX::Context& graphics, D3D12_GPU_VIRTUAL_ADDRESS cameraAddress,
                                               const MeshPassResources& passResources) {
    MATH::Vec3 cameraPosition{};
    const MeshFrameResources& frame = GetActiveMeshFrameResources();
    if (frame.view.IsInitialized()) {
        cameraPosition = {
            frame.view.camera.cameraPos.x,
            frame.view.camera.cameraPos.y,
            frame.view.camera.cameraPos.z,
        };
    }
    return ExecuteParticleForwardTransparent(gMeshRendererState.particlePass, graphics, cameraAddress, cameraPosition,
                                             passResources);
}

bool ExecutePreparedWorldSpriteForwardOpaque(const GFX::Context& graphics, D3D12_GPU_VIRTUAL_ADDRESS cameraAddress) {
    if (!gMeshRendererState.worldSpritePass.HasOpaqueWork()) {
        return true;
    }
    return gMeshRendererState.worldSpritePass.ExecuteForwardOpaque(graphics, cameraAddress,
                                                                   ResolveSurfaceLightingBindings());
}

bool ExecutePreparedWorldSpriteForwardTransparent(const GFX::Context& graphics, D3D12_GPU_VIRTUAL_ADDRESS cameraAddress,
                                                  const MeshPassResources& passResources) {
    if (!gMeshRendererState.worldSpritePass.HasTransparentWork()) {
        return true;
    }
    const auto* capture = passResources.worldSpriteDepthOfFieldCapture;
    if (capture != nullptr && gMeshRendererState.worldSpritePass.GetStats().depthOfFieldCaptureReady &&
        graphics.cmdList != nullptr && passResources.compositionDepth.ptr != 0u) {
        graphics.cmdList->OMSetRenderTargets(static_cast<UINT>(capture->renderTargets.size()),
                                             capture->renderTargets.data(), FALSE, &passResources.compositionDepth);
        const bool captured = gMeshRendererState.worldSpritePass.ExecuteForwardTransparentDepthOfField(
            graphics, cameraAddress, ResolveSurfaceLightingBindings(), *capture);
        if (captured) {
            RENDER3D::DOF::MarkWorldSpriteLayerCapture(capture->frameIndex);
        }
        const bool rebound = passResources.compositionTarget.Rebind();
        if (!rebound) {
            return false;
        }
        if (captured) {
            return true;
        }
    }
    return gMeshRendererState.worldSpritePass.ExecuteForwardTransparent(graphics, cameraAddress,
                                                                        ResolveSurfaceLightingBindings());
}

bool ExecutePreparedWorldTextForwardTransparent(const GFX::Context& graphics, D3D12_GPU_VIRTUAL_ADDRESS cameraAddress) {
    if (!gMeshRendererState.worldTextPass.HasTransparentWork()) {
        return true;
    }
    return gMeshRendererState.worldTextPass.ExecuteForwardTransparent(graphics, cameraAddress);
}

} // namespace HIKARI::MESHRENDERER::INTERNAL

namespace HIKARI::MESHRENDERER {

using namespace INTERNAL;
bool RenderGeometryAuxPass(const GFX::Context& graphics, RENDER3D::SURFACE::SceneSurfaceBuffer& surfaceBuffer,
                           D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv, bool writeFeatureMask) {
    return RenderGeometryAuxPassInternal(graphics, surfaceBuffer, sceneDsv, writeFeatureMask);
}

bool HasStylizedFeatureMaskBackend() {
    const auto& meshletStats = gMeshRendererState.meshletRenderBackend.GetStats();
    const bool traditionalReady = gMeshRendererState.pipelines.geometryFeatureMaskPso != nullptr &&
                                  gMeshRendererState.pipelines.geometryFeatureMaskSkinnedPso != nullptr;
    return RENDER3D::GPUDRIVEN::IsGeometryFeatureBackendReady(RENDER3D::GPUDRIVEN::ResolveGeometryBackendPolicy(),
                                                              meshletStats.geometryFeatureMaskPipelineReady,
                                                              traditionalReady);
}

bool HasGeometryOutlineFeatureBackend() {
    const auto& meshletStats = gMeshRendererState.meshletRenderBackend.GetStats();
    const bool traditionalReady = gMeshRendererState.pipelines.geometryOutlinePso != nullptr &&
                                  gMeshRendererState.pipelines.geometryOutlineSkinnedPso != nullptr;
    return RENDER3D::GPUDRIVEN::IsGeometryFeatureBackendReady(RENDER3D::GPUDRIVEN::ResolveGeometryBackendPolicy(),
                                                              meshletStats.geometryOutlinePipelineReady,
                                                              traditionalReady);
}

bool HasVisibilityOverlayFeatureBackend() {
    const auto& meshletStats = gMeshRendererState.meshletRenderBackend.GetStats();
    const bool traditionalReady = gMeshRendererState.pipelines.visibilityOverlayPso != nullptr &&
                                  gMeshRendererState.pipelines.visibilityOverlaySkinnedPso != nullptr;
    return RENDER3D::GPUDRIVEN::IsGeometryFeatureBackendReady(RENDER3D::GPUDRIVEN::ResolveGeometryBackendPolicy(),
                                                              meshletStats.visibilityOverlayPipelineReady,
                                                              traditionalReady);
}

bool RenderGeometryOutlineFeaturePass(const GFX::Context& graphics, const MeshPassResources& passResources) {
    GFX::PIX::ScopedGpuEvent event(graphics.cmdList, GFX::PIX::kColorRender, "RenderFeature.GeometryOutline");
    GFX::GPU_PROFILE::ScopedGpuTimer timer(graphics.cmdList, GFX::GPU_PROFILE::Pass::StylizedGeometryOutline);
    return ExecuteMaterialScopedFeaturePasses(graphics, passResources, MeshDrawPassKind::GeometryOutline,
                                              RENDER3D::MESHLET::MeshletPipelineKind::GeometryOutline);
}

bool RenderVisibilityOverlayFeaturePass(const GFX::Context& graphics, const MeshPassResources& passResources) {
    GFX::PIX::ScopedGpuEvent event(graphics.cmdList, GFX::PIX::kColorRender, "RenderFeature.VisibilityOverlay");
    GFX::GPU_PROFILE::ScopedGpuTimer timer(graphics.cmdList, GFX::GPU_PROFILE::Pass::StylizedVisibilityOverlay);
    return ExecuteMaterialScopedFeaturePasses(graphics, passResources, MeshDrawPassKind::VisibilityOverlay,
                                              RENDER3D::MESHLET::MeshletPipelineKind::VisibilityOverlay);
}

bool HasDepthPrepassWork() {
    return HasGpuDrivenPassSource(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass) ||
           gMeshRendererState.worldSpritePass.HasOpaqueWork();
}

bool RenderDepthPrepass(const GFX::Context& graphics, D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv, bool useCullingCamera) {
    return RenderDepthPrepassInternal(graphics, sceneDsv, useCullingCamera);
}

bool RenderWaterVisibilityDepth(const GFX::Context& graphics, D3D12_CPU_DESCRIPTOR_HANDLE waterDsv,
                                const MeshPassResources& passResources) {
    return RenderWaterVisibilityDepthInternal(graphics, waterDsv, passResources);
}

bool FinalizeGpuDrivenVisibilityWithoutDepth(const GFX::Context& graphics) {
    if (!HasGpuDrivenSceneSource()) {
        return false;
    }

    GFX::PIX::ScopedGpuEvent pixFinalize(graphics.cmdList, GFX::PIX::kColorUpload,
                                         "GpuDepthVisibility.FinalizeVisibility.NoHZB");
    BuildGpuDrivenWorkFrame(graphics, nullptr, MakeMainCameraGpuDrivenPassMask(), true);
    SyncGpuDrivenBackendAvailability();
    BuildStrictGpuDrivenCommandFrame(graphics);
    return true;
}

bool FinalizeGpuDrivenVisibilityFromDepth(const GFX::Context& graphics,
                                          const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& depthVisibilityStats) {
    const HIKARI::RENDER3D::DEPTH::DepthPyramidView& depthPyramid = depthVisibilityStats.depthPyramid;
    if (!HasGpuDrivenSceneSource() || !depthPyramid.valid || depthPyramid.pyramidSrv.ptr == 0 ||
        depthPyramid.width == 0 || depthPyramid.height == 0 || !depthPyramid.viewProjValid) {
        return false;
    }

    GFX::PIX::ScopedGpuEvent pixFinalize(graphics.cmdList, GFX::PIX::kColorUpload,
                                         "GpuDepthVisibility.FinalizeVisibility");
    BuildGpuDrivenWorkFrame(graphics, &depthPyramid, MakeMainCameraGpuDrivenPassMask(), true);
    SyncGpuDrivenBackendAvailability();
    BuildStrictGpuDrivenCommandFrame(graphics);
    return true;
}

bool PrepareForwardReflectionCapture(const GFX::Context& graphics, bool captureMaterial) {
    if (gMeshRendererState.worldSpritePass.HasOpaqueWork())
        return false;
    return !HasGpuDrivenPassSource(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque) ||
           PrepareForwardReflectionCapturePipelines(graphics, captureMaterial);
}

RENDER3D::WORLD_SPRITES::WorldSpritePass* PrepareWorldSpritesForView(const GFX::Context& graphics,
                                                                     const RENDER3D::RenderViewContext& view) {
    if (!view.cameraFrame || !view.cameraFrame->valid)
        return nullptr;
    auto& sprites = gMeshRendererState.worldSpritePass;
    return sprites.PrepareFrame(graphics, view.worldSpriteFrameSource, view.cameraFrame->camera) ? &sprites : nullptr;
}

bool RenderForwardOpaquePass(const GFX::Context& graphics, const MeshPassResources& passResources) {
    bool surfaceExecuted = true;
    if (HasGpuDrivenPassSource(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque)) {
        const GeometryBackendExecutionResult backendResult = ExecuteGeometryBackendPlan(
            graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque, passResources, MeshDrawPassKind::Forward,
            RENDER3D::MESHLET::MeshletPipelineKind::ForwardOpaque);
        const bool massSubmissionResident =
            gMeshRendererState.gpuDrivenLayer.GetSceneUploadStats().massSceneResident &&
            gMeshRendererState.gpuDrivenLayer.GetFrameContext().scene.massInstanceCount != 0u;
        if (massSubmissionResident && backendResult.meshShaderExecuted) {
            gMeshRendererState.massForwardOpaqueSubmissionCommitted = true;
        }
        surfaceExecuted = backendResult.gpuBackendExecuted;
    }
    const MeshFrameResources& frame = GetActiveMeshFrameResources();
    const bool spritesExecuted = ExecutePreparedWorldSpriteForwardOpaque(graphics, frame.view.Bindings().camera);
    return surfaceExecuted && spritesExecuted;
}

bool RenderForwardTransparentPass(const GFX::Context& graphics, const MeshPassResources& passResources) {
    bool surfaceExecuted = true;
    if (HasGpuDrivenPassSource(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent)) {
        const GeometryBackendExecutionResult backendResult = ExecuteGeometryBackendPlan(
            graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent, passResources,
            MeshDrawPassKind::Forward, RENDER3D::MESHLET::MeshletPipelineKind::ForwardTransparent);
        surfaceExecuted = backendResult.gpuBackendExecuted;
    }
    const MeshFrameResources& frame = GetActiveMeshFrameResources();
    const bool spritesExecuted =
        ExecutePreparedWorldSpriteForwardTransparent(graphics, frame.view.Bindings().camera, passResources);
    const bool particlesExecuted =
        ExecutePreparedParticleForwardTransparent(graphics, frame.view.Bindings().camera, passResources);
    const bool textExecuted = ExecutePreparedWorldTextForwardTransparent(graphics, frame.view.Bindings().camera);
    return surfaceExecuted && spritesExecuted && particlesExecuted && textExecuted;
}

bool HasDepthAwarePassWork() {
    return HasGpuDrivenPassSource(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware);
}

bool HasForwardTransparentPassWork() {
    return HasGpuDrivenPassSource(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent) ||
           gMeshRendererState.worldSpritePass.HasTransparentWork() ||
           gMeshRendererState.worldTextPass.HasTransparentWork() || gMeshRendererState.particlePass.HasDrawWork();
}

bool RenderDepthAwarePass(const GFX::Context& graphics, const MeshPassResources& passResources) {
    if (!HasGpuDrivenPassSource(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware)) {
        return true;
    }
    const GeometryBackendExecutionResult backendResult = ExecuteGeometryBackendPlan(
        graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware, passResources, MeshDrawPassKind::Forward,
        RENDER3D::MESHLET::MeshletPipelineKind::ForwardDepthAware);
    return backendResult.gpuBackendExecuted;
}

bool SetAmbientOcclusionRuntimeEnabled(bool enabled) {
    auto& view = GetActiveMeshFrameResources().view;
    if (!view.IsInitialized())
        return false;
    const float value = enabled ? 1.0f : 0.0f;
    if (view.skyEnvironment.ambientSamplingParams.x == value)
        return true;
    const float previous = view.skyEnvironment.ambientSamplingParams.x;
    view.skyEnvironment.ambientSamplingParams.x = value;
    if (view.PublishSkyEnvironment(GFX::GetCurrentContext()))
        return true;
    view.skyEnvironment.ambientSamplingParams.x = previous;
    return false;
}

} // namespace HIKARI::MESHRENDERER
