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
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::MESHRENDERER {

    using namespace INTERNAL;

    namespace {

        // Runtime の scene が空でも Tool Preview は独立して material source
        // を解決する。通常の BeginFrame が skip されたフレームでも、現在の
        // triple-buffer slot と texture load budget を明示的に開始する。
        constexpr size_t kEditorInteractiveMaterialTextureLoadsPerFrame = 4u;

        bool BeginEditorInteractiveMaterialFrame(
            const GFX::Context& graphics) {

            gMeshRendererState.frameResources.Activate(graphics.frameIndex);
            if (!gMeshRendererState.frameResources.HasActiveDrawResources()) {
                return false;
            }
            gMeshRendererState.materialResolver.BeginFrame(
                kEditorInteractiveMaterialTextureLoadsPerFrame);
            gMeshRendererState.gpuMaterialRegistry.BeginFrame();
            return true;
        }

    } // namespace

    EditorInteractiveMeshRenderResult RenderEditorInteractiveOpaque(
        const RENDER3D::RenderViewContext& view,
        const SceneEnvironment& environment,
        const RENDER3D::LIGHTING::SceneLightingSet& lighting,
        uint32_t screenWidth,
        uint32_t screenHeight,
        const EditorInteractiveRenderSettings& settings) {

        const GFX::Context graphics = GFX::GetCurrentContext();
        if (view.cameraFrame == nullptr ||
            !view.cameraFrame->valid ||
            screenWidth == 0 ||
            screenHeight == 0 ||
            graphics.cmdList == nullptr ||
            !EnsureInitialized(graphics) ||
            !EnsureEditorInteractiveResources()) {
            return EditorInteractiveMeshRenderResult::RendererUnavailable;
        }
        if (!BeginEditorInteractiveMaterialFrame(graphics)) {
            return EditorInteractiveMeshRenderResult::
                MaterialFrameUnavailable;
        }

        EditorInteractiveMeshFrameResources& editorFrame =
            gMeshRendererState.editorInteractive.frames[
                graphics.frameIndex % GFX::kFrameResourceCount];
        if (!editorFrame.IsReady()) {
            return EditorInteractiveMeshRenderResult::
                FrameResourcesUnavailable;
        }

        std::optional<IBL::ScopedEnvironmentOverride> iblOverride{};
        if (settings.iblEnvironmentOverride != nullptr) {
            iblOverride.emplace(*settings.iblEnvironmentOverride);
        }
        std::optional<
            REFLECTION::
                ScopedReflectionProbeSamplingSuppress>
            reflectionProbeSuppress{};
        std::optional<
            RENDER3D::LIGHTPROBE::
                ScopedLightProbeVolumeSamplingSuppress>
            lightProbeSuppress{};
        SceneEnvironment isolatedEnvironment{};
        const SceneEnvironment* renderEnvironment = &environment;
        if (settings.suppressSpatialLightingProviders) {
            reflectionProbeSuppress.emplace();
            lightProbeSuppress.emplace();
            isolatedEnvironment = environment;
            isolatedEnvironment.surfaceLightmaps.enabled = false;
            renderEnvironment = &isolatedEnvironment;
        }

        const Camera3D& camera = view.cameraFrame->camera;
        CameraCB& cameraData = editorFrame.view.camera;
        const MATH::Vec3 cameraPosition = camera.GetPosition();
        const MATH::Mat4 viewProj = camera.GetViewProj();
        FillCameraCB(viewProj, MATH::Inverse(viewProj), cameraPosition,
            screenWidth, screenHeight, cameraData);
        editorFrame.view.ClearLocalLights();
        if (!settings.neutralLighting) {
            editorFrame.view.BuildLocalLights(lighting, camera, screenWidth, screenHeight);
        }
        FillLightCB(
            *renderEnvironment,
            editorFrame.view.localLightList,
            settings.debugView,
            editorFrame.view.light,
            gMeshRendererState.debugStats);
        FillShadowCB(
            *renderEnvironment,
            GFX::GetCurrentContext().frameIndex,
            editorFrame.view.shadow);
        FillSkyEnvironmentCB(
            *renderEnvironment,
            cameraPosition,
            camera.GetTarget(),
            editorFrame.view.skyEnvironment);
        if (settings.neutralLighting) {
            LightCB& light = editorFrame.view.light;
            light.directionalDir = { 0.35f, -0.82f, 0.45f, 0.0f };
            light.directionalColor = { 1.0f, 0.98f, 0.94f, 1.0f };
            light.directionalIntensity = 0.75f;
            light.ambientColor = { 0.92f, 0.96f, 1.0f, 1.0f };
            light.ambientIntensity = 0.55f;
            light.localLightCount = 0;
            light.fogParams.x = 0.0f;
            editorFrame.view.shadow.enabled = 0;
        }
        if (settings.neutralClayMaterial) {
            editorFrame.view.skyEnvironment = {};
        }
        if (!editorFrame.view.Publish(graphics)) {
            return EditorInteractiveMeshRenderResult::FrameResourcesUnavailable;
        }

        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& sceneSource =
            settings.sceneSourceOverride != nullptr
                ? *settings.sceneSourceOverride
                : gMeshRendererState.gpuDrivenSceneSource;
        const auto* jointPalettes = sceneSource.meshShaderJointPalettes;
        if (jointPalettes != nullptr) {
            const size_t paletteCount =
                (std::min)(
                    jointPalettes->size(),
                    static_cast<size_t>(kMaxObjectCount));
            for (size_t paletteIndex = 0;
                paletteIndex < paletteCount;
                ++paletteIndex) {

                const std::vector<MATH::Mat4>& palette =
                    (*jointPalettes)[paletteIndex];
                if (!palette.empty()) {
                    (void)UploadJointPalette(
                        editorFrame.jointPaletteMapped,
                        paletteIndex,
                        palette);
                }
            }
        }
        const uintptr_t sourceIdentity =
            reinterpret_cast<uintptr_t>(&sceneSource);
        SyncGpuDrivenSceneMaterialSources(
            graphics,
            sourceIdentity,
            sceneSource);
        if (!gMeshRendererState.particlePass.PrepareFrame(
                graphics,
                settings.particleFrameSource,
                gMeshRendererState.gpuMaterialRegistry)) {
            return EditorInteractiveMeshRenderResult::
                MaterialFrameUnavailable;
        }
        if (!gMeshRendererState.editorInteractive.worldSpritePass.PrepareFrame(
                graphics,
                view.worldSpriteFrameSource,
                cameraPosition)) {
            return EditorInteractiveMeshRenderResult::
                MaterialFrameUnavailable;
        }
        if (!gMeshRendererState.editorInteractive.worldTextPass.PrepareFrame(
                graphics,
                view.worldTextFrameSource,
                cameraPosition)) {
            return EditorInteractiveMeshRenderResult::
                MaterialFrameUnavailable;
        }
        if (!CommitActiveMaterialDataFrame(graphics.cmdList)) {
            return EditorInteractiveMeshRenderResult::MaterialFrameUnavailable;
        }

        RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer =
            gMeshRendererState.editorInteractive.gpuDrivenLayer;
        layer.BeginFrame(&sceneSource);
        RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadDesc uploadDescription{};
        uploadDescription.commandList = GFX::GetCurrentContext().cmdList;
        uploadDescription.residency =
            &gMeshRendererState.editorInteractive.sceneResidency;
        uploadDescription.frameIndex = GFX::GetCurrentContext().frameIndex;
        const std::span<const uint32_t> materialBindings =
            gMeshRendererState.gpuMaterialRegistry.GetSourceBindings(
                sourceIdentity);
        uploadDescription.materialSlotBySourceRecord =
            materialBindings.data();
        uploadDescription.materialSourceRecordCount =
            materialBindings.size();
        uploadDescription.materialBindingVersion =
            gMeshRendererState.gpuMaterialRegistry.GetBindingVersion(
                sourceIdentity);
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadStats& uploadStats =
            layer.UploadSceneFrame(uploadDescription);
        if (sceneSource.CountGpuSceneInstances() != 0u &&
            !uploadStats.sceneResident) {
            return EditorInteractiveMeshRenderResult::
                SceneResidencyUnavailable;
        }
        RecordOceanSpectrumForWaterFrame(
            graphics,
            gMeshRendererState.editorInteractive.waterSurfaceGpuFrameBuffer);

        RENDER3D::GPUDRIVEN::GpuDrivenBackendAvailability availability{};
        const RENDER3D::MESHLET::MeshletRenderBackendStats& meshletStats =
            gMeshRendererState.meshletRenderBackend.GetStats();
        availability.meshShaderForwardPipelineReady =
            meshletStats.forwardPipelineReady &&
            meshletStats.depthAwarePipelineReady &&
            meshletStats.transparentPipelineReady;
        availability.meshShaderGeometryAuxPipelineReady =
            meshletStats.geometryAuxPipelineReady;
        layer.SetBackendAvailability(availability);

        const MATH::Mat4 cullViewProj = cameraData.viewProj;
        ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            GFX::GetCurrentContext().cmdList->SetDescriptorHeaps(1, heaps);
        }

        const RENDER3D::GPUDRIVEN::GpuDrivenFrame gpuDrivenFrame =
            RENDER3D::GPUDRIVEN::BuildGpuDrivenFrame(
                RENDER3D::GPUDRIVEN::BuildGpuDrivenFrameInput(sceneSource));
        RENDER3D::GPUDRIVEN::ClusterGpuDrivenProducerAdapter& producer =
            gMeshRendererState.editorInteractive.clusterGpuDrivenProducer;
        producer.BeginFrame(false);
        RENDER3D::GPUDRIVEN::GpuDrivenWorkContext workContext{};
        workContext.producer = &producer;
        workContext.commandList = GFX::GetCurrentContext().cmdList;
        workContext.viewProj = cullViewProj;
        workContext.cameraPosition = cameraPosition;
        workContext.geometryPoolSrv =
            RENDER3D::GetClusterGeometryPoolSrvGpuHandle(graphics);
        workContext.surfaceGpuSceneGpuAddress =
            gMeshRendererState.editorInteractive.surfaceGpuSceneBuffer.
                GetGpuVirtualAddress();
        workContext.frame = &gpuDrivenFrame;
        workContext.passMask =
            RENDER3D::GPUDRIVEN::MakeGpuDrivenPassMask(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque) |
            RENDER3D::GPUDRIVEN::MakeGpuDrivenPassMask(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware) |
            RENDER3D::GPUDRIVEN::MakeGpuDrivenPassMask(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent);
        if (settings.writeStylizedFeatureMask) {
            workContext.passMask |=
                RENDER3D::GPUDRIVEN::MakeGpuDrivenPassMask(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::GeometryAux);
        }
        workContext.collectCounterReadback = false;
        (void)RENDER3D::GPUDRIVEN::BuildGpuDrivenWork(workContext);
        layer.ImportProducerOutput(producer.BuildFrameOutput());

        RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDescription{};
        commandFrameDescription.commandList = GFX::GetCurrentContext().cmdList;
        commandFrameDescription.cullViewProj = &cullViewProj;
        commandFrameDescription.frameIndex = GFX::GetCurrentContext().frameIndex;
        commandFrameDescription.enableSurfaceFrustumCull = true;
        layer.BuildCommandFrame(commandFrameDescription);
        GFX::GetCurrentContext().cmdList->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        MeshFrameBindingOverrides overrides{};
        overrides.cameraAddress =
            editorFrame.view.Bindings().camera;
        overrides.cullingCameraAddress = overrides.cameraAddress;
        overrides.lightAddress =
            editorFrame.view.Bindings().light;
        overrides.localLightsAddress =
            editorFrame.view.Bindings().localLights.lights;
        overrides.localLightClusterHeadersAddress =
            editorFrame.view.Bindings().localLights.clusterHeaders;
        overrides.localLightClusterIndicesAddress =
            editorFrame.view.Bindings().localLights.clusterIndices;
        overrides.shadowAddress =
            editorFrame.view.Bindings().shadow;
        overrides.skyEnvironmentAddress =
            editorFrame.view.Bindings().skyEnvironment;
        overrides.jointPaletteAddress =
            editorFrame.jointPaletteCB->GetGPUVirtualAddress();
        overrides.surfaceGpuSceneFrameBuffer =
            &gMeshRendererState.editorInteractive.surfaceGpuSceneBuffer;
        overrides.waterSurfaceGpuFrameBuffer =
            &gMeshRendererState.editorInteractive.waterSurfaceGpuFrameBuffer;

        MeshPassResources passResources = settings.passResources;
        passResources.fallbackAoTextureHandle =
            gMeshRendererState.fallbackTextureHandle;
        const bool stylizedSurfacePassReady =
            ExecuteEditorInteractiveStylizedSurfacePass(
                graphics,
                screenWidth,
                screenHeight,
                settings,
                passResources,
                layer,
                overrides);
        const GeometryBackendExecutionResult opaqueResult =
            ExecuteEditorInteractiveGeometryBackendPlan(
                graphics,
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque,
                passResources,
                MeshDrawPassKind::Forward,
                RENDER3D::MESHLET::MeshletPipelineKind::ForwardOpaque,
                layer,
                overrides);
        const bool worldSpriteOpaqueResult =
            gMeshRendererState.editorInteractive.worldSpritePass.
                ExecuteForwardOpaque(graphics, overrides.cameraAddress);

        const bool hasDepthAwareSource =
            layer.GetPassExecutionState(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::
                    ForwardDepthAware).hasSource;
        const bool hasTransparentSource =
            layer.GetPassExecutionState(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::
                    ForwardTransparent).hasSource;
        if (hasDepthAwareSource || hasTransparentSource) {
            const RENDER3D::RenderTextureView opaqueSceneColor =
                settings.captureSceneColor != nullptr
                    ? settings.captureSceneColor(
                        settings.targetAccess.userData)
                    : RENDER3D::RenderTextureView{};
            const bool targetReady = settings.targetAccess.Rebind();
            if (!opaqueSceneColor.IsSampleable() || !targetReady) {
                return EditorInteractiveMeshRenderResult::ForwardPassFailed;
            }
            passResources.opaqueSceneColor = opaqueSceneColor;
        }

        GeometryBackendExecutionResult depthAwareResult{};
        const bool needsDepthRead = hasDepthAwareSource ||
            settings.renderDepthAwareOverlay != nullptr;
        if (needsDepthRead) {
            if ((hasDepthAwareSource &&
                 !passResources.sceneDepth.IsSampleable()) ||
                !settings.targetAccess.BeginDepthRead()) {
                return EditorInteractiveMeshRenderResult::ForwardPassFailed;
            }
            if (hasDepthAwareSource) {
                depthAwareResult =
                    ExecuteEditorInteractiveGeometryBackendPlan(
                        graphics,
                        RENDER3D::GPUDRIVEN::GpuDrivenPassKind::
                            ForwardDepthAware,
                        passResources,
                        MeshDrawPassKind::Forward,
                        RENDER3D::MESHLET::MeshletPipelineKind::
                            ForwardDepthAware,
                        layer,
                        overrides);
            }
            if (settings.renderDepthAwareOverlay != nullptr) {
                settings.renderDepthAwareOverlay(
                    settings.depthAwareOverlayUserData);
            }
            settings.targetAccess.EndDepthRead();
        }
        GeometryBackendExecutionResult transparentResult{};
        bool particleResult = true;
        bool worldSpriteTransparentResult = true;
        bool worldTextTransparentResult = true;
        const bool hasParticleWork =
            gMeshRendererState.particlePass.HasDrawWork();
        const bool hasWorldSpriteTransparentWork =
            gMeshRendererState.editorInteractive.worldSpritePass.
                HasTransparentWork();
        const bool hasWorldTextTransparentWork =
            gMeshRendererState.editorInteractive.worldTextPass.
                HasTransparentWork();
        if (hasTransparentSource || hasParticleWork ||
            hasWorldSpriteTransparentWork ||
            hasWorldTextTransparentWork) {
            if (!passResources.sceneDepth.IsSampleable() ||
                !settings.targetAccess.BeginDepthRead()) {
                return EditorInteractiveMeshRenderResult::ForwardPassFailed;
            }
            transparentResult = ExecuteEditorInteractiveGeometryBackendPlan(
                graphics,
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent,
                passResources,
                MeshDrawPassKind::Forward,
                RENDER3D::MESHLET::MeshletPipelineKind::ForwardTransparent,
                layer,
                overrides);
            worldSpriteTransparentResult =
                gMeshRendererState.editorInteractive.worldSpritePass.
                    ExecuteForwardTransparent(
                        graphics, overrides.cameraAddress);
            particleResult = ExecutePreparedParticleForwardTransparent(
                graphics,
                overrides.cameraAddress,
                passResources);
            worldTextTransparentResult =
                gMeshRendererState.editorInteractive.worldTextPass.
                    ExecuteForwardTransparent(
                        graphics, overrides.cameraAddress);
            settings.targetAccess.EndDepthRead();
        }
        const bool sceneRendered = opaqueResult.gpuBackendExecuted ||
            depthAwareResult.gpuBackendExecuted ||
            transparentResult.gpuBackendExecuted ||
            gMeshRendererState.editorInteractive.worldSpritePass.
                HasFrameWork() ||
            gMeshRendererState.editorInteractive.worldTextPass.
                HasFrameWork() ||
            gMeshRendererState.particlePass.HasDrawWork() ||
            !sceneSource.HasAnyGpuSceneRanges();
        if (!sceneRendered || !worldSpriteOpaqueResult ||
            !worldSpriteTransparentResult || !particleResult) {
            return EditorInteractiveMeshRenderResult::ForwardPassFailed;
        }
        if (!worldTextTransparentResult) {
            return EditorInteractiveMeshRenderResult::ForwardPassFailed;
        }
        if (!stylizedSurfacePassReady) {
            return EditorInteractiveMeshRenderResult::
                StylizedSurfacePassFailed;
        }
        if (!ExecuteEditorInteractiveStylizedGeometryPasses(
                graphics,
                settings,
                passResources,
                layer,
                overrides)) {
            return EditorInteractiveMeshRenderResult::
                StylizedGeometryPassFailed;
        }
        return EditorInteractiveMeshRenderResult::Ready;
    }

    void ReleaseEditorInteractiveSceneSource(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source) {

        if (source != nullptr) {
            gMeshRendererState.gpuMaterialRegistry.ReleaseSourceBindings(
                reinterpret_cast<uintptr_t>(source));
        }
    }

    void ShutdownEditorInteractiveResources() {
        ResetEditorInteractiveResources();
    }

} // namespace HIKARI::MESHRENDERER

#endif
