#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Gfx/HIKARI_GfxContext.h"

#if defined(HIKARI_WITH_EDITOR)

#include <algorithm>
#include <span>

#include "Core/HIKARI_TimeService.h"
#include "Render3D/Core/MeshRenderer/Bindings/HIKARI_MeshResourceBindings.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshVariantResolver.h"
#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenWorkBuilder.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::MESHRENDERER {

    using namespace INTERNAL;

    namespace {

        bool RenderEditorInteractiveMainlineStatic(
            const GFX::Context& graphics,
            const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& sceneSource,
            const MeshPassResources& passResources,
            const MeshFrameBindingOverrides& overrides) {

            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass =
                sceneSource.GetPass(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);
            if (pass.instances == nullptr ||
                pass.materialSources == nullptr ||
                pass.instances->empty() ||
                graphics.cmdList == nullptr) {
                return false;
            }

            const size_t instanceCount = (std::min)(
                pass.instances->size(),
                pass.materialSources->size());
            MeshBindingStateCache bindingCache{};
            MeshDrawContext drawContext = BuildDrawContext(
                graphics,
                false,
                MeshDrawPassKind::Forward,
                passResources,
                &overrides);
            drawContext.binding.cache = &bindingCache;
            BindSurfaceRecordFrameResources(drawContext);
            BindObjectDataIndex(drawContext.binding, 0u);
            BindMaterialDataIndex(drawContext.binding, 0u);

            bool rendered = false;
            for (size_t instanceIndex = 0;
                instanceIndex < instanceCount;
                ++instanceIndex) {

                const RENDER3D::RUNTIME::SurfaceGpuSceneInstance& instance =
                    (*pass.instances)[instanceIndex];
                const RENDER3D::RUNTIME::SurfaceGpuSceneMaterialSource& source =
                    (*pass.materialSources)[instanceIndex];
                if (source.model == nullptr ||
                    instance.meshIndex >= source.model->meshes.size()) {
                    continue;
                }

                const MeshAsset& meshAsset =
                    source.model->meshes[instance.meshIndex];
                if (instance.primitiveIndex >= meshAsset.primitives.size()) {
                    continue;
                }
                const MeshPrimitive& primitive =
                    meshAsset.primitives[instance.primitiveIndex];
                if (primitive.layout != VertexLayoutKind::StaticPNTT) {
                    continue;
                }

                Mesh* mesh = gMeshRendererState.primitiveCache.GetOrCreateStatic(
                    GFX::GetCurrentContext().device,
                    primitive,
                    &gMeshRendererState.debugStats);
                if (mesh == nullptr || !mesh->IsValid()) {
                    continue;
                }

                DrawItem variantItem{};
                variantItem.asset = source.model;
                variantItem.materialOverride = source.materialOverride;
                variantItem.fxFlags = source.fxFlags;
                for (size_t fxIndex = 0;
                    fxIndex < VFX::kMaterialFxUserCount;
                    ++fxIndex) {

                    variantItem.fxValues[fxIndex] = source.fxUser[fxIndex];
                }
                ResolveDrawVariant(variantItem);
                const MaterialAsset* materialAsset =
                    primitive.materialIndex < source.model->materials.size()
                        ? &source.model->materials[primitive.materialIndex]
                        : nullptr;
                const VFX::VariantKey variant = ResolvePrimitiveVariant(
                    variantItem,
                    source.materialOverride != nullptr
                        ? nullptr
                        : materialAsset,
                    &primitive);
                ID3D12PipelineState* pipelineState = ResolveTraditionalStaticPso(
                    graphics,
                    MeshDrawPassKind::Forward,
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque,
                    variant);
                if (pipelineState == nullptr) {
                    continue;
                }

                BindPipelineState(drawContext.binding, pipelineState);
                BindSurfaceGpuSceneControl(
                    drawContext.binding,
                    pass.gpuSceneBaseIndex +
                        static_cast<uint32_t>(instanceIndex),
                    true);
                const D3D12_VERTEX_BUFFER_VIEW vertexBuffer =
                    mesh->GetVBView();
                const D3D12_INDEX_BUFFER_VIEW indexBuffer =
                    mesh->GetIBView();
                GFX::GetCurrentContext().cmdList->IASetVertexBuffers(
                    0,
                    1,
                    &vertexBuffer);
                GFX::GetCurrentContext().cmdList->IASetIndexBuffer(&indexBuffer);
                GFX::GetCurrentContext().cmdList->DrawIndexedInstanced(
                    mesh->GetIndexCount(),
                    1u,
                    0u,
                    0,
                    0u);
                rendered = true;
            }
            return rendered;
        }

    } // namespace

    bool RenderEditorInteractiveOpaque(
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
            !gMeshRendererState.frameResources.HasActiveDrawResources() ||
            !EnsureEditorInteractiveResources()) {
            return false;
        }

        EditorInteractiveMeshFrameResources& editorFrame =
            gMeshRendererState.editorInteractive.frames[
                graphics.frameIndex % GFX::kFrameResourceCount];
        if (!editorFrame.IsReady()) {
            return false;
        }

        const Camera3D& camera = view.cameraFrame->camera;
        CameraCB& cameraData = *editorFrame.cameraMapped;
        cameraData = {};
        cameraData.viewProj = camera.GetViewProj();
        cameraData.invViewProj = MATH::Inverse(cameraData.viewProj);
        const MATH::Vec3 cameraPosition = camera.GetPosition();
        cameraData.cameraPos = {
            cameraPosition.x,
            cameraPosition.y,
            cameraPosition.z,
            1.0f
        };
        const FrameContext& timeFrame = TIME::GetFrameContext();
        cameraData.timeParams = {
            gMeshRendererState.elapsedTimeSec,
            timeFrame.unscaledDt,
            timeFrame.gameDt,
            static_cast<float>(timeFrame.frameIndex)
        };
        cameraData.screenParams = {
            static_cast<float>(screenWidth),
            static_cast<float>(screenHeight),
            1.0f / static_cast<float>(screenWidth),
            1.0f / static_cast<float>(screenHeight)
        };
        editorFrame.localLightListView = {};
        if (!settings.neutralLighting) {
            editorFrame.localLightListView =
                RENDER3D::LIGHTING::BuildLocalLightList(
                    lighting,
                    camera,
                    screenWidth,
                    screenHeight,
                    {
                        editorFrame.localLightMapped,
                        RENDER3D::LIGHTING::kMaximumLocalLightCount,
                        editorFrame.localLightClusterHeaderMapped,
                        RENDER3D::LIGHTING::
                            kMaximumLocalLightClusterCount,
                        editorFrame.localLightClusterIndexMapped,
                        RENDER3D::LIGHTING::
                            kMaximumLocalLightClusterIndexCount
                    });
        }
        FillLightCB(
            environment,
            editorFrame.localLightListView,
            settings.debugView,
            *editorFrame.lightMapped,
            gMeshRendererState.debugStats);
        FillShadowCB(environment, *editorFrame.shadowMapped);
        FillSkyEnvironmentCB(
            environment,
            cameraPosition,
            camera.GetTarget(),
            *editorFrame.skyEnvironmentMapped);
        if (settings.neutralLighting) {
            LightCB& light = *editorFrame.lightMapped;
            light.directionalDir = { 0.35f, -0.82f, 0.45f, 0.0f };
            light.directionalColor = { 1.0f, 0.98f, 0.94f, 1.0f };
            light.directionalIntensity = 0.75f;
            light.ambientColor = { 0.92f, 0.96f, 1.0f, 1.0f };
            light.ambientIntensity = 0.55f;
            light.specularParams.x = 0.12f;
            light.localLightCount = 0;
            light.fogParams.x = 0.0f;
            editorFrame.shadowMapped->enabled = 0;
        }
        if (settings.neutralClayMaterial) {
            *editorFrame.skyEnvironmentMapped = {};
        }

        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& sceneSource =
            settings.sceneSourceOverride != nullptr
                ? *settings.sceneSourceOverride
                : gMeshRendererState.gpuDrivenSceneSource;
        const uintptr_t sourceIdentity =
            reinterpret_cast<uintptr_t>(&sceneSource);
        SyncGpuDrivenSceneMaterialSources(
            graphics,
            sourceIdentity,
            sceneSource);
        CommitActiveMaterialDataFrame(graphics.cmdList);

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
            return false;
        }

        RENDER3D::GPUDRIVEN::GpuDrivenBackendAvailability availability{};
        const RENDER3D::MESHLET::MeshletRenderBackendStats& meshletStats =
            gMeshRendererState.meshletRenderBackend.GetStats();
        availability.meshShaderForwardPipelineReady =
            meshletStats.forwardPipelineReady &&
            meshletStats.depthAwarePipelineReady &&
            meshletStats.transparentPipelineReady;
        availability.meshShaderGeometryAuxPipelineReady =
            meshletStats.geometryAuxPipelineReady;
        availability.traditionalIndirectPipelineReady =
            GetStaticRootSignature(gMeshRendererState.pipelines) != nullptr &&
            GetSkinnedRootSignature(gMeshRendererState.pipelines) != nullptr &&
            gMeshRendererState.pipelines.pso != nullptr &&
            gMeshRendererState.pipelines.skinnedPso != nullptr;
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
            editorFrame.cameraCB->GetGPUVirtualAddress();
        overrides.cullingCameraAddress = overrides.cameraAddress;
        overrides.lightAddress =
            editorFrame.lightCB->GetGPUVirtualAddress();
        overrides.localLightsAddress =
            editorFrame.localLightBuffer->GetGPUVirtualAddress();
        overrides.localLightClusterHeadersAddress =
            editorFrame.localLightClusterHeaderBuffer->
                GetGPUVirtualAddress();
        overrides.localLightClusterIndicesAddress =
            editorFrame.localLightClusterIndexBuffer->
                GetGPUVirtualAddress();
        overrides.shadowAddress =
            editorFrame.shadowCB->GetGPUVirtualAddress();
        overrides.skyEnvironmentAddress =
            editorFrame.skyEnvironmentCB->GetGPUVirtualAddress();
        overrides.surfaceGpuSceneFrameBuffer =
            &gMeshRendererState.editorInteractive.surfaceGpuSceneBuffer;
        overrides.traditionalCommandStreamBuffer =
            &gMeshRendererState.editorInteractive.traditionalCommandStreamBuffer;

        MeshPassResources passResources{};
        passResources.fallbackAoTextureHandle =
            gMeshRendererState.fallbackTextureHandle;
        bool stylizedFeaturePassesReady =
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
        const GeometryBackendExecutionResult depthAwareResult =
            ExecuteEditorInteractiveGeometryBackendPlan(
                graphics,
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware,
                passResources,
                MeshDrawPassKind::Forward,
                RENDER3D::MESHLET::MeshletPipelineKind::ForwardDepthAware,
                layer,
                overrides);
        const GeometryBackendExecutionResult transparentResult =
            ExecuteEditorInteractiveGeometryBackendPlan(
                graphics,
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent,
                passResources,
                MeshDrawPassKind::Forward,
                RENDER3D::MESHLET::MeshletPipelineKind::ForwardTransparent,
                layer,
                overrides);
        if (stylizedFeaturePassesReady) {
            stylizedFeaturePassesReady =
                ExecuteEditorInteractiveStylizedGeometryPasses(
                    graphics,
                    settings,
                    passResources,
                    layer,
                    overrides);
        }
        const bool primaryGpuReady = layer.IsBackendConsumable(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque,
            RENDER3D::GPUDRIVEN::GeometryBackendKind::GpuDrivenMeshShader);
        const bool fallbackRendered = !primaryGpuReady &&
            RenderEditorInteractiveMainlineStatic(
                graphics,
                sceneSource,
                passResources,
                overrides);
        const bool sceneRendered = opaqueResult.gpuBackendExecuted ||
            depthAwareResult.gpuBackendExecuted ||
            transparentResult.gpuBackendExecuted ||
            fallbackRendered ||
            !sceneSource.HasAnyGpuSceneRanges();
        return sceneRendered && stylizedFeaturePassesReady;
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
