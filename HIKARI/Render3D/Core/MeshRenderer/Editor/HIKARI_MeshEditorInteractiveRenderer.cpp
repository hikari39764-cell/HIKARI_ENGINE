#include "Render3D/Core/HIKARI_MeshRenderer.h"

#if defined(HIKARI_WITH_EDITOR)

#include <algorithm>

#include "Core/HIKARI_TimeService.h"
#include "HIKARI_Services.h"
#include "Render3D/Core/MeshRenderer/Bindings/HIKARI_MeshResourceBindings.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshPipelineStore.h"
#include "Render3D/Core/MeshRenderer/Pipeline/HIKARI_MeshVariantResolver.h"
#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::MESHRENDERER {

    using namespace INTERNAL;

    namespace {

        bool RenderEditorInteractiveMainlineStatic(
            const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& sceneSource,
            const MeshPassResources& passResources,
            const MeshFrameBindingOverrides& overrides) {

            const RENDER3D::GPUDRIVEN::GpuDrivenPassSource& pass =
                sceneSource.GetPass(
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque);
            if (pass.instances == nullptr ||
                pass.materialSources == nullptr ||
                pass.instances->empty() ||
                SERVICES::gCtx.cmdList == nullptr) {
                return false;
            }

            const size_t instanceCount = (std::min)(
                pass.instances->size(),
                pass.materialSources->size());
            MeshBindingStateCache bindingCache{};
            MeshDrawContext drawContext = BuildDrawContext(
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
                    SERVICES::gCtx.device,
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
                SERVICES::gCtx.cmdList->IASetVertexBuffers(
                    0,
                    1,
                    &vertexBuffer);
                SERVICES::gCtx.cmdList->IASetIndexBuffer(&indexBuffer);
                SERVICES::gCtx.cmdList->DrawIndexedInstanced(
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
        uint32_t screenWidth,
        uint32_t screenHeight,
        const EditorInteractiveRenderSettings& settings) {

        if (view.cameraFrame == nullptr ||
            !view.cameraFrame->valid ||
            screenWidth == 0 ||
            screenHeight == 0 ||
            SERVICES::gCtx.cmdList == nullptr ||
            !EnsureInitialized() ||
            !gMeshRendererState.frameResources.HasActiveDrawResources() ||
            !EnsureEditorInteractiveResources()) {
            return false;
        }

        EditorInteractiveMeshFrameResources& editorFrame =
            gMeshRendererState.editorInteractive.frames[
                SERVICES::gCtx.frameIndex % GFX::kFrameResourceCount];
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
        FillLightCB(
            environment,
            settings.debugView,
            *editorFrame.lightMapped,
            gMeshRendererState.debugStats);
        FillShadowCB(environment, *editorFrame.shadowMapped);
        FillSkyEnvironmentCB(
            environment,
            *editorFrame.skyEnvironmentMapped);
        if (settings.neutralLighting) {
            LightCB& light = *editorFrame.lightMapped;
            light.directionalDir = { 0.35f, -0.82f, 0.45f, 0.0f };
            light.directionalColor = { 1.0f, 0.98f, 0.94f, 1.0f };
            light.directionalIntensity = 0.75f;
            light.ambientColor = { 0.92f, 0.96f, 1.0f, 1.0f };
            light.ambientIntensity = 0.55f;
            light.specularParams.x = 0.12f;
            light.pointLightCount = 0;
            light.fogParams.x = 0.0f;
            editorFrame.shadowMapped->enabled = 0;
        }

        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& sceneSource =
            settings.sceneSourceOverride != nullptr
                ? *settings.sceneSourceOverride
                : gMeshRendererState.gpuDrivenSceneSource;
        RENDER3D::GPUDRIVEN::GpuDrivenLayer& layer =
            gMeshRendererState.editorInteractive.gpuDrivenLayer;
        layer.BeginFrame(&sceneSource);
        RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadDesc uploadDescription{};
        uploadDescription.commandList = SERVICES::gCtx.cmdList;
        uploadDescription.residency =
            &gMeshRendererState.editorInteractive.sceneResidency;
        uploadDescription.frameIndex = SERVICES::gCtx.frameIndex;
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneUploadStats& uploadStats =
            layer.UploadSceneFrame(uploadDescription);
        if (sceneSource.CountGpuSceneInstances() != 0u &&
            !uploadStats.sceneResident) {
            return false;
        }

        RENDER3D::GPUDRIVEN::GpuDrivenBackendAvailability availability{};
        availability.traditionalIndirectPipelineReady =
            GetStaticRootSignature(gMeshRendererState.pipelines) != nullptr &&
            GetSkinnedRootSignature(gMeshRendererState.pipelines) != nullptr &&
            gMeshRendererState.pipelines.pso != nullptr &&
            gMeshRendererState.pipelines.skinnedPso != nullptr;
        layer.SetBackendAvailability(availability);

        const MATH::Mat4 cullViewProj = cameraData.viewProj;
        RENDER3D::GPUDRIVEN::GpuDrivenCommandFrameDesc commandFrameDescription{};
        commandFrameDescription.commandList = SERVICES::gCtx.cmdList;
        commandFrameDescription.cullViewProj = &cullViewProj;
        commandFrameDescription.frameIndex = SERVICES::gCtx.frameIndex;
        commandFrameDescription.enableSurfaceFrustumCull = true;
        layer.BuildCommandFrame(commandFrameDescription);

        ID3D12DescriptorHeap* srvHeap = RENDER3D::GetTextureResourceSrvHeap();
        if (srvHeap != nullptr) {
            ID3D12DescriptorHeap* heaps[] = { srvHeap };
            SERVICES::gCtx.cmdList->SetDescriptorHeaps(1, heaps);
        }
        SERVICES::gCtx.cmdList->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        if (!sceneSource.GetPass(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque)
                .HasGpuSceneRange()) {
            return true;
        }

        MeshFrameBindingOverrides overrides{};
        overrides.cameraAddress =
            editorFrame.cameraCB->GetGPUVirtualAddress();
        overrides.cullingCameraAddress = overrides.cameraAddress;
        overrides.lightAddress =
            editorFrame.lightCB->GetGPUVirtualAddress();
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
        const bool mainlineRendered =
            RenderEditorInteractiveMainlineStatic(
                sceneSource,
                passResources,
                overrides);
        const bool sidecarRendered = ExecuteTraditionalDrawFrame(
            passResources,
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque,
            MeshDrawPassKind::Forward,
            &layer,
            &overrides);
        return mainlineRendered || sidecarRendered;
    }

    void ShutdownEditorInteractiveResources() {
        ResetEditorInteractiveResources();
    }

} // namespace HIKARI::MESHRENDERER

#endif
