#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Gfx/HIKARI_GfxContext.h"

#if defined(HIKARI_WITH_EDITOR)

#include <algorithm>
#include <optional>
#include <span>
#include <vector>

#include "Core/HIKARI_TimeService.h"
#include "Render3D/Core/MeshRenderer/Bindings/HIKARI_MeshResourceBindings.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"
#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenWorkBuilder.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneJointPaletteUpload.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::MESHRENDERER {

using namespace INTERNAL;

namespace {

// Runtime の scene が空でも Tool Preview は独立して material source
// を解決する。通常の BeginFrame が skip されたフレームでも、現在の
// triple-buffer slot と texture load budget を明示的に開始する。
constexpr size_t kEditorInteractiveMaterialTextureLoadsPerFrame = 4u;

bool BeginEditorInteractiveMaterialFrame(const GFX::Context& graphics) {
    gMeshRendererState.frameResources.Activate(graphics.frameIndex);
    if (!gMeshRendererState.frameResources.HasActiveDrawResources()) {
        return false;
    }
    gMeshRendererState.materialResolver.BeginFrame(kEditorInteractiveMaterialTextureLoadsPerFrame);
    gMeshRendererState.gpuMaterialRegistry.BeginFrame();
    return true;
}

} // namespace

EditorInteractiveMeshRenderResult RenderEditorInteractiveOpaque(const RENDER3D::RenderViewContext& view,
                                                                const SceneEnvironment& environment,
                                                                const RENDER3D::LIGHTING::SceneLightingSet& lighting,
                                                                uint32_t screenWidth, uint32_t screenHeight,
                                                                const EditorInteractiveRenderSettings& settings) {
    const GFX::Context graphics = GFX::GetCurrentContext();
    if (view.cameraFrame == nullptr || !view.cameraFrame->valid || screenWidth == 0 || screenHeight == 0 ||
        graphics.cmdList == nullptr || !EnsureInitialized(graphics) || !EnsureEditorInteractiveResources()) {
        return EditorInteractiveMeshRenderResult::RendererUnavailable;
    }
    if (!BeginEditorInteractiveMaterialFrame(graphics)) {
        return EditorInteractiveMeshRenderResult::MaterialFrameUnavailable;
    }

    EditorInteractiveMeshFrameResources& editorFrame =
        gMeshRendererState.editorInteractive.frames[graphics.frameIndex % GFX::kFrameResourceCount];
    if (!editorFrame.IsReady()) {
        return EditorInteractiveMeshRenderResult::FrameResourcesUnavailable;
    }

    std::optional<IBL::ScopedEnvironmentOverride> iblOverride{};
    if (settings.iblEnvironmentOverride != nullptr) {
        iblOverride.emplace(*settings.iblEnvironmentOverride);
    }
    std::optional<REFLECTION::ScopedReflectionProbeSamplingSuppress> reflectionProbeSuppress{};
    std::optional<RENDER3D::LIGHTPROBE::ScopedLightProbeVolumeSamplingSuppress> lightProbeSuppress{};
    if (settings.suppressSpatialLightingProviders) {
        reflectionProbeSuppress.emplace();
        lightProbeSuppress.emplace();
    }

    const Camera3D& camera = view.cameraFrame->camera;
    CameraCB& cameraData = editorFrame.view.camera;
    const MATH::Vec3 cameraPosition = camera.GetPosition();
    const MATH::Mat4 viewProj = camera.GetViewProj();
    // 流体の煙と粒子の場景深度碰撞が使う相機相対の viewProj（抖動なし）。
    const MATH::Mat4 relativeViewProj = RENDER3D::FLUID::MakeCameraRelativeViewProj(camera.GetView(), camera.GetProj());
    FillCameraCB(viewProj, MATH::Inverse(viewProj), cameraPosition, screenWidth, screenHeight, cameraData);
    if (!settings.neutralLighting) {
        editorFrame.view.BuildLocalLights(lighting, camera, screenWidth, screenHeight);
    } else {
        editorFrame.view.ClearLocalLights();
    }
    FillLightCB(environment, editorFrame.view.localLightList, settings.debugView, editorFrame.view.light,
                gMeshRendererState.debugStats);
    FillShadowCB(environment, GFX::GetCurrentContext().frameIndex, editorFrame.view.shadow);
    FillSkyEnvironmentCB(environment, cameraPosition, camera.GetTarget(), editorFrame.view.skyEnvironment);
    // 編集器のビューは SSAO を描かないので AO を読まない。主ビューは描いた時だけ有効にする（SetAmbientOcclusionRuntimeEnabled）
    // が、ここで環境の設定のまま読むと予備の貼图の値で環境光が消え、影になった面が真っ黒になる。
    editorFrame.view.skyEnvironment.ambientSamplingParams.x = 0.0f;
    if (settings.suppressSpatialLightingProviders) {
        editorFrame.view.skyEnvironment.ambientSamplingParams.w = 0.0f;
    }
    if (settings.neutralLighting) {
        // 中立照明は露出に依らない表示参照の明るさ。shader の前露出 P を打ち消す値で書く。
        LightCB& light = editorFrame.view.light;
        const float displayToScene = light.viewExposure.y;
        light.directionalDir = {0.35f, -0.82f, 0.45f, 0.0f};
        light.directionalColor = {1.0f, 0.98f, 0.94f, 1.0f};
        light.directionalIntensity = 0.75f * displayToScene;
        light.ambientColor = {0.92f, 0.96f, 1.0f, 1.0f};
        light.ambientIntensity = 0.55f * displayToScene;
        light.localLightCount = 0;
        light.fogParams.x = 0.0f;
        editorFrame.view.shadow.enabled = 0;
    }
    if (settings.neutralLighting) {
        // 空の環境光も物理量なので、中立照明では使わない（上の平行光と環境光だけで見せる）。
        editorFrame.view.skyEnvironment = {};
    }
    if (!editorFrame.view.Publish(graphics)) {
        return EditorInteractiveMeshRenderResult::FrameResourcesUnavailable;
    }

    const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& sceneSource = settings.sceneSourceOverride != nullptr
                                                                       ? *settings.sceneSourceOverride
                                                                       : gMeshRendererState.gpuDrivenSceneSource;
    editorFrame.jointPaletteAddress =
        RENDER3D::GPUDRIVEN::UploadGpuSceneJointPaletteSnapshot(graphics, sceneSource.meshShaderJointPalettes);
    if (editorFrame.jointPaletteAddress == 0u) {
        return EditorInteractiveMeshRenderResult::FrameResourcesUnavailable;
    }
    const uintptr_t sourceIdentity = reinterpret_cast<uintptr_t>(&sceneSource);
    SyncGpuDrivenSceneMaterialSources(graphics, sourceIdentity, sceneSource);
    PrepareFluidFrame(graphics, settings.particleFrameSource, cameraPosition);
    const RENDER3D::FLUID::FluidFlowView fluidFlow = gMeshRendererState.fluidPass.GetFlowView();
    if (!gMeshRendererState.particlePass.PrepareFrame(graphics, settings.particleFrameSource,
                                                      gMeshRendererState.gpuMaterialRegistry, &fluidFlow)) {
        return EditorInteractiveMeshRenderResult::MaterialFrameUnavailable;
    }
    if (!gMeshRendererState.editorInteractive.worldSpritePass.PrepareFrame(graphics, view.worldSpriteFrameSource,
                                                                           camera)) {
        return EditorInteractiveMeshRenderResult::MaterialFrameUnavailable;
    }
    if (!gMeshRendererState.editorInteractive.worldTextPass.PrepareFrame(graphics, view.worldTextFrameSource,
                                                                         cameraPosition)) {
        return EditorInteractiveMeshRenderResult::MaterialFrameUnavailable;
    }
    if (!CommitActiveMaterialDataFrame(graphics.cmdList)) {
        return EditorInteractiveMeshRenderResult::MaterialFrameUnavailable;
    }

    RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer = gMeshRendererState.editorInteractive.gpuDrivenLayer;
    layer.BeginFrame(&sceneSource);
    RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadDesc uploadDescription{};
    uploadDescription.commandList = GFX::GetCurrentContext().cmdList;
    uploadDescription.residency = &gMeshRendererState.editorInteractive.sceneResidency;
    uploadDescription.frameIndex = GFX::GetCurrentContext().frameIndex;
    const std::span<const uint32_t> materialBindings =
        gMeshRendererState.gpuMaterialRegistry.GetSourceBindings(sourceIdentity);
    uploadDescription.materialSlotBySourceRecord = materialBindings.data();
    uploadDescription.materialSourceRecordCount = materialBindings.size();
    uploadDescription.materialBindingVersion = gMeshRendererState.gpuMaterialRegistry.GetBindingVersion(sourceIdentity);
    const RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadStats& uploadStats = layer.UploadSceneFrame(uploadDescription);
    if (sceneSource.CountGpuSceneInstances() != 0u && !uploadStats.sceneResident) {
        return EditorInteractiveMeshRenderResult::SceneResidencyUnavailable;
    }
    RecordOceanSpectrumForWaterFrame(graphics, gMeshRendererState.editorInteractive.waterSurfaceGpuFrameBuffer);

    RENDER3D::GPUDRIVEN::GpuDrivenBackendAvailability availability{};
    const RENDER3D::MESHLET::MeshletRenderBackendStats& meshletStats =
        gMeshRendererState.meshletRenderBackend.GetStats();
    availability.meshShaderForwardPipelineReady = meshletStats.forwardPipelineReady &&
                                                  meshletStats.depthAwarePipelineReady &&
                                                  meshletStats.transparentPipelineReady;
    availability.meshShaderGeometryAuxPipelineReady = meshletStats.geometryAuxPipelineReady;
    layer.SetBackendAvailability(availability);

    const MATH::Mat4 cullViewProj = cameraData.viewProj;
    ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
    if (srvHeap != nullptr) {
        ID3D12DescriptorHeap* heaps[] = {srvHeap};
        GFX::GetCurrentContext().cmdList->SetDescriptorHeaps(1, heaps);
    }

    const RENDER3D::GPUDRIVEN::GpuDrivenFrame gpuDrivenFrame =
        RENDER3D::GPUDRIVEN::BuildGpuDrivenFrame(RENDER3D::GPUDRIVEN::BuildGpuDrivenFrameInput(sceneSource));
    RENDER3D::GPUDRIVEN::ClusterGpuDrivenProducerAdapter& producer =
        gMeshRendererState.editorInteractive.clusterGpuDrivenProducer;
    producer.BeginFrame(false);
    RENDER3D::GPUDRIVEN::GpuDrivenWorkContext workContext{};
    workContext.producer = &producer;
    workContext.commandList = GFX::GetCurrentContext().cmdList;
    workContext.viewProj = cullViewProj;
    workContext.cameraPosition = cameraPosition;
    workContext.geometryPoolSrv = RENDER3D::GetClusterGeometryPoolSrvGpuHandle(graphics);
    workContext.surfaceGpuSceneGpuAddress =
        gMeshRendererState.editorInteractive.surfaceGpuSceneBuffer.GetGpuVirtualAddress();
    workContext.frame = &gpuDrivenFrame;
    workContext.passMask =
        RENDER3D::GPUDRIVEN::MakeGpuDrivenPassMask(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque) |
        RENDER3D::GPUDRIVEN::MakeGpuDrivenPassMask(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware) |
        RENDER3D::GPUDRIVEN::MakeGpuDrivenPassMask(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent);
    if (settings.writeStylizedFeatureMask) {
        workContext.passMask |=
            RENDER3D::GPUDRIVEN::MakeGpuDrivenPassMask(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::GeometryAux);
    }
    workContext.collectCounterReadback = false;
    (void)RENDER3D::GPUDRIVEN::BuildGpuDrivenWork(workContext);
    layer.ImportProducerOutput(producer.BuildFrameOutput());

    RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDescription{};
    commandFrameDescription.commandList = GFX::GetCurrentContext().cmdList;
    commandFrameDescription.cullViewProj = &cullViewProj;
    commandFrameDescription.renderViewProj = &cameraData.viewProj;
    commandFrameDescription.screenHeight = static_cast<float>(screenHeight);
    commandFrameDescription.frameIndex = GFX::GetCurrentContext().frameIndex;
    commandFrameDescription.enableSurfaceFrustumCull = true;
    layer.BuildCommandFrame(commandFrameDescription);
    GFX::GetCurrentContext().cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    MeshFrameBindingOverrides overrides{};
    overrides.cameraAddress = editorFrame.view.Bindings().camera;
    overrides.cullingCameraAddress = overrides.cameraAddress;
    overrides.lightAddress = editorFrame.view.Bindings().light;
    overrides.localLightsAddress = editorFrame.view.Bindings().localLights.lights;
    overrides.localLightClusterHeadersAddress = editorFrame.view.Bindings().localLights.clusterHeaders;
    overrides.localLightClusterIndicesAddress = editorFrame.view.Bindings().localLights.clusterIndices;
    overrides.shadowAddress = editorFrame.view.Bindings().shadow;
    overrides.skyEnvironmentAddress = editorFrame.view.Bindings().skyEnvironment;
    overrides.jointPaletteAddress = editorFrame.jointPaletteAddress;
    overrides.surfaceGpuSceneFrameBuffer = &gMeshRendererState.editorInteractive.surfaceGpuSceneBuffer;
    overrides.waterSurfaceGpuFrameBuffer = &gMeshRendererState.editorInteractive.waterSurfaceGpuFrameBuffer;

    MeshPassResources passResources = settings.passResources;
    passResources.compositionTarget = settings.targetAccess;
    passResources.fallbackAoTextureHandle = gMeshRendererState.fallbackTextureHandle;
    const bool stylizedSurfacePassReady = ExecuteEditorInteractiveStylizedSurfacePass(
        graphics, screenWidth, screenHeight, settings, passResources, layer, overrides);
    const GeometryBackendExecutionResult opaqueResult = ExecuteEditorInteractiveGeometryBackendPlan(
        graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque, passResources, MeshDrawPassKind::Forward,
        RENDER3D::MESHLET::MeshletPipelineKind::ForwardOpaque, layer, overrides);
    const bool worldSpriteOpaqueResult =
        gMeshRendererState.editorInteractive.worldSpritePass.ExecuteForwardOpaque(graphics, overrides.cameraAddress);

    const bool hasDepthAwareSource =
        layer.GetPassExecutionState(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware).hasSource;
    const bool hasTransparentSource =
        layer.GetPassExecutionState(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent).hasSource;
    if (hasDepthAwareSource || hasTransparentSource) {
        const RENDER3D::RenderTextureView opaqueSceneColor =
            settings.captureSceneColor != nullptr ? settings.captureSceneColor(settings.targetAccess.userData)
                                                  : RENDER3D::RenderTextureView{};
        const bool targetReady = settings.targetAccess.Rebind();
        if (!opaqueSceneColor.IsSampleable() || !targetReady) {
            return EditorInteractiveMeshRenderResult::ForwardPassFailed;
        }
        passResources.opaqueSceneColor = opaqueSceneColor;
    }

    GeometryBackendExecutionResult depthAwareResult{};
    const bool needsDepthRead = hasDepthAwareSource || settings.renderDepthAwareOverlay != nullptr;
    if (needsDepthRead) {
        if ((hasDepthAwareSource && !passResources.sceneDepth.IsSampleable()) ||
            !settings.targetAccess.BeginDepthRead()) {
            return EditorInteractiveMeshRenderResult::ForwardPassFailed;
        }
        if (hasDepthAwareSource) {
            depthAwareResult = ExecuteEditorInteractiveGeometryBackendPlan(
                graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware, passResources,
                MeshDrawPassKind::Forward, RENDER3D::MESHLET::MeshletPipelineKind::ForwardDepthAware, layer, overrides);
        }
        if (settings.renderDepthAwareOverlay != nullptr) {
            settings.renderDepthAwareOverlay(settings.depthAwareOverlayUserData);
        }
        settings.targetAccess.EndDepthRead();
    }
    // 流体の煙：水の後、透明物体の前に重ねる（粒子は相機体積で手前の煙を受ける）。描けないフレームは煙だけを飛ばす。
    if (gMeshRendererState.fluidPass.HasRenderableDomains() && passResources.sceneDepth.IsSampleable() &&
        settings.sceneColorFormat != DXGI_FORMAT_UNKNOWN && settings.targetAccess.BeginDepthRead()) {
        RENDER3D::FLUID::FluidRenderView fluidView =
            RENDER3D::FLUID::MakeFluidRenderView(environment, view.viewId.value, relativeViewProj, cameraPosition);
        fluidView.renderWidth = screenWidth;
        fluidView.renderHeight = screenHeight;
        fluidView.sceneDepthSrv = passResources.sceneDepth.srv;
        fluidView.sceneColorFormat = settings.sceneColorFormat;
        (void)RenderFluidVolumes(graphics, fluidView, settings.targetAccess);
        settings.targetAccess.EndDepthRead();
        if (!settings.targetAccess.Rebind()) {
            return EditorInteractiveMeshRenderResult::ForwardPassFailed;
        }
    }
    GeometryBackendExecutionResult transparentResult{};
    bool particleResult = true;
    bool worldSpriteTransparentResult = true;
    bool worldTextTransparentResult = true;
    const bool hasParticleWork = gMeshRendererState.particlePass.HasDrawWork();
    const bool hasWorldSpriteTransparentWork =
        gMeshRendererState.editorInteractive.worldSpritePass.HasTransparentWork();
    const bool hasWorldTextTransparentWork = gMeshRendererState.editorInteractive.worldTextPass.HasTransparentWork();
    if (hasTransparentSource || hasParticleWork || hasWorldSpriteTransparentWork || hasWorldTextTransparentWork) {
        if (!passResources.sceneDepth.IsSampleable() || !settings.targetAccess.BeginDepthRead()) {
            return EditorInteractiveMeshRenderResult::ForwardPassFailed;
        }
        transparentResult = ExecuteEditorInteractiveGeometryBackendPlan(
            graphics, RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent, passResources,
            MeshDrawPassKind::Forward, RENDER3D::MESHLET::MeshletPipelineKind::ForwardTransparent, layer, overrides);
        worldSpriteTransparentResult = gMeshRendererState.editorInteractive.worldSpritePass.ExecuteForwardTransparent(
            graphics, overrides.cameraAddress);
        // 編集器の対話視図は自分の相機で描き、その深度も粒子の場景深度碰撞へ撮る（主視图より先に撮ったフレームは、
        // この視図の深度で碰撞する）。
        particleResult = ExecuteParticleForwardTransparent(
            gMeshRendererState.particlePass, graphics, overrides.cameraAddress, cameraPosition, passResources,
            gMeshRendererState.fluidPass.GetCameraVolume(), &relativeViewProj);
        worldTextTransparentResult = gMeshRendererState.editorInteractive.worldTextPass.ExecuteForwardTransparent(
            graphics, overrides.cameraAddress);
        settings.targetAccess.EndDepthRead();
    }
    const bool sceneRendered = opaqueResult.gpuBackendExecuted || depthAwareResult.gpuBackendExecuted ||
                               transparentResult.gpuBackendExecuted ||
                               gMeshRendererState.editorInteractive.worldSpritePass.HasFrameWork() ||
                               gMeshRendererState.editorInteractive.worldTextPass.HasFrameWork() ||
                               gMeshRendererState.particlePass.HasDrawWork() || !sceneSource.HasAnyGpuSceneRanges();
    if (!sceneRendered || !worldSpriteOpaqueResult || !worldSpriteTransparentResult || !particleResult) {
        return EditorInteractiveMeshRenderResult::ForwardPassFailed;
    }
    if (!worldTextTransparentResult) {
        return EditorInteractiveMeshRenderResult::ForwardPassFailed;
    }
    if (!stylizedSurfacePassReady) {
        return EditorInteractiveMeshRenderResult::StylizedSurfacePassFailed;
    }
    if (!ExecuteEditorInteractiveStylizedGeometryPasses(graphics, settings, passResources, layer, overrides)) {
        return EditorInteractiveMeshRenderResult::StylizedGeometryPassFailed;
    }
    return EditorInteractiveMeshRenderResult::Ready;
}

void ReleaseEditorInteractiveSceneSource(const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source) {
    if (source != nullptr) {
        gMeshRendererState.gpuMaterialRegistry.ReleaseSourceBindings(reinterpret_cast<uintptr_t>(source));
    }
}

void ShutdownEditorInteractiveResources() { ResetEditorInteractiveResources(); }

} // namespace HIKARI::MESHRENDERER

#endif
