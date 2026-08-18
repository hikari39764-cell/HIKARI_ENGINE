#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Gfx/HIKARI_GfxContext.h"

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

        bool ExecuteMaterialScopedFeaturePasses(
            const GFX::Context& graphics,
            const MeshPassResources& passResources,
            MeshDrawPassKind drawPass,
            RENDER3D::MESHLET::MeshletPipelineKind meshletPipeline) {

            bool hasSource = false;
            bool allSourcesExecuted = true;
            SyncGpuDrivenBackendAvailability();
            for (const RENDER3D::GPUDRIVEN::GpuDrivenPassKind sourcePass :
                 kMaterialFeaturePasses) {
                if (!HasGpuDrivenPassSource(sourcePass) ||
                    gMeshRendererState.gpuDrivenLayer.
                        GetPassExecutionPlan(sourcePass).gpuBackendCount == 0u) {
                    continue;
                }
                hasSource = true;
                allSourcesExecuted &= ExecuteGeometryBackendPlan(
                    graphics,
                    sourcePass,
                    passResources,
                    drawPass,
                    meshletPipeline).gpuBackendExecuted;
            }
            return hasSource && allSourcesExecuted;
        }
    }

    bool RenderGeometryAuxPassInternal(
        const GFX::Context& graphics,
        RENDER3D::SURFACE::SceneSurfaceBuffer& surfaceBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv,
        bool writeFeatureMask) {
        if (!HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque)) {
            return false;
        }

        const CameraCB* camera = GetActiveMeshFrameResources().cameraMapped;
        const uint32_t width = static_cast<uint32_t>(
            (std::max)(1.0f, camera ? camera->screenParams.x : 1.0f));
        const uint32_t height = static_cast<uint32_t>(
            (std::max)(1.0f, camera ? camera->screenParams.y : 1.0f));
        if (!surfaceBuffer.EnsureSize(
                graphics, width, height, writeFeatureMask)) {
            return false;
        }

        if (sceneDsv.ptr == 0) {
            return false;
        }

        GFX::PIX::ScopedGpuEvent pixGeometry(
            graphics.cmdList,
            GFX::PIX::kColorRender,
            "SceneSurface");
        surfaceBuffer.BeginWrite(graphics.cmdList, sceneDsv);
        MeshPassResources passResources{};
        const GeometryBackendExecutionResult backendResult =
            ExecuteGeometryBackendPlan(
                graphics,
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::GeometryAux,
                passResources,
                writeFeatureMask
                    ? MeshDrawPassKind::GeometryAuxFeatureMask
                    : MeshDrawPassKind::GeometryAux,
                writeFeatureMask
                    ? RENDER3D::MESHLET::MeshletPipelineKind::
                        GeometryAuxFeatureMask
                    : RENDER3D::MESHLET::MeshletPipelineKind::GeometryAux);
        surfaceBuffer.EndWrite(graphics.cmdList);
        return backendResult.gpuBackendExecuted;
    }

    bool RenderDepthPrepassInternal(
        const GFX::Context& graphics,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv) {
        if (!HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass)) {
            return false;
        }

        ID3D12GraphicsCommandList* cmd = graphics.cmdList;
        if (cmd == nullptr || sceneDsv.ptr == 0) {
            return false;
        }

        const CameraCB* camera = GetActiveMeshFrameResources().cameraMapped;
        const uint32_t width = static_cast<uint32_t>(
            (std::max)(1.0f, camera ? camera->screenParams.x : 1.0f));
        const uint32_t height = static_cast<uint32_t>(
            (std::max)(1.0f, camera ? camera->screenParams.y : 1.0f));
        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MaxDepth = 1.0f;
        D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };

        GFX::PIX::ScopedGpuEvent pixDepth(
            cmd,
            GFX::PIX::kColorRender,
            "GpuDepthVisibility.DepthPrepass");
        cmd->OMSetRenderTargets(0, nullptr, FALSE, &sceneDsv);
        cmd->RSSetViewports(1, &viewport);
        cmd->RSSetScissorRects(1, &scissor);

        const MeshPassResources passResources{};
        const GeometryBackendExecutionResult backendResult =
            ExecuteDepthVisibilityBackendPlan(
                graphics,
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass,
                passResources);
        return backendResult.gpuBackendExecuted;
    }

    bool RenderWaterVisibilityDepthInternal(
        const GFX::Context& graphics,
        D3D12_CPU_DESCRIPTOR_HANDLE waterDsv,
        const MeshPassResources& passResources) {

        if (!HasGpuDrivenPassSource(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware) ||
            graphics.cmdList == nullptr ||
            waterDsv.ptr == 0 ||
            !passResources.sceneDepth.IsSampleable()) {
            return false;
        }

        const CameraCB* camera = GetActiveMeshFrameResources().cameraMapped;
        const uint32_t width = static_cast<uint32_t>(
            (std::max)(1.0f, camera ? camera->screenParams.x :
                static_cast<float>(passResources.sceneDepth.width)));
        const uint32_t height = static_cast<uint32_t>(
            (std::max)(1.0f, camera ? camera->screenParams.y :
                static_cast<float>(passResources.sceneDepth.height)));
        if (width != passResources.sceneDepth.width ||
            height != passResources.sceneDepth.height) {
            return false;
        }

        D3D12_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MaxDepth = 1.0f;
        D3D12_RECT scissor{
            0,
            0,
            static_cast<LONG>(width),
            static_cast<LONG>(height)};

        GFX::PIX::ScopedGpuEvent pixWater(
            graphics.cmdList,
            GFX::PIX::kColorRender,
            "GpuDepthVisibility.Water");
        graphics.cmdList->OMSetRenderTargets(
            0,
            nullptr,
            FALSE,
            &waterDsv);
        graphics.cmdList->RSSetViewports(1, &viewport);
        graphics.cmdList->RSSetScissorRects(1, &scissor);

        const GeometryBackendExecutionResult backendResult =
            ExecuteGeometryBackendPlan(
                graphics,
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware,
                passResources,
                MeshDrawPassKind::WaterVisibility,
                RENDER3D::MESHLET::MeshletPipelineKind::WaterVisibility);
        return backendResult.gpuBackendExecuted;
    }

    bool ExecutePreparedParticleForwardTransparent(
        const GFX::Context& graphics,
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddress) {

        if (!gMeshRendererState.particlePass.HasDrawWork()) {
            return true;
        }
        const MeshFrameResources& frame = GetActiveMeshFrameResources();
        RENDER3D::PARTICLES::ParticleGpuDrivenDrawBindings bindings{};
        bindings.camera = cameraAddress;
        bindings.materialData = frame.materialDataBuffer != nullptr
            ? frame.materialDataBuffer->GetGPUVirtualAddress()
            : 0u;
        bindings.materialParameters =
            frame.materialProgramParameterBuffer != nullptr
                ? frame.materialProgramParameterBuffer->
                    GetGPUVirtualAddress()
                : 0u;
        bindings.materialResources =
            frame.materialProgramResourceBuffer != nullptr
                ? frame.materialProgramResourceBuffer->
                    GetGPUVirtualAddress()
                : 0u;
        return gMeshRendererState.particlePass.
            ExecuteForwardTransparent(graphics, bindings);
    }


} // namespace HIKARI::MESHRENDERER::INTERNAL

namespace HIKARI::MESHRENDERER {

    using namespace INTERNAL;
    bool RenderGeometryAuxPass(
        const GFX::Context& graphics,
        RENDER3D::SURFACE::SceneSurfaceBuffer& surfaceBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv,
        bool writeFeatureMask) {
        return RenderGeometryAuxPassInternal(
            graphics, surfaceBuffer, sceneDsv, writeFeatureMask);
    }

    bool HasStylizedFeatureMaskBackend() {
        const auto& meshletStats =
            gMeshRendererState.meshletRenderBackend.GetStats();
        const bool traditionalReady =
            gMeshRendererState.pipelines.geometryFeatureMaskPso != nullptr &&
            gMeshRendererState.pipelines.geometryFeatureMaskSkinnedPso != nullptr;
        return RENDER3D::GPUDRIVEN::IsGeometryFeatureBackendReady(
            RENDER3D::GPUDRIVEN::ResolveGeometryBackendPolicy(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque),
            meshletStats.geometryFeatureMaskPipelineReady,
            traditionalReady);
    }

    bool HasGeometryOutlineFeatureBackend() {
        const auto& meshletStats =
            gMeshRendererState.meshletRenderBackend.GetStats();
        const bool traditionalReady =
            gMeshRendererState.pipelines.geometryOutlinePso != nullptr &&
            gMeshRendererState.pipelines.geometryOutlineSkinnedPso != nullptr;
        return RENDER3D::GPUDRIVEN::IsGeometryFeatureBackendReady(
            RENDER3D::GPUDRIVEN::ResolveGeometryBackendPolicy(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque),
            meshletStats.geometryOutlinePipelineReady,
            traditionalReady);
    }

    bool HasVisibilityOverlayFeatureBackend() {
        const auto& meshletStats =
            gMeshRendererState.meshletRenderBackend.GetStats();
        const bool traditionalReady =
            gMeshRendererState.pipelines.visibilityOverlayPso != nullptr &&
            gMeshRendererState.pipelines.visibilityOverlaySkinnedPso != nullptr;
        return RENDER3D::GPUDRIVEN::IsGeometryFeatureBackendReady(
            RENDER3D::GPUDRIVEN::ResolveGeometryBackendPolicy(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque),
            meshletStats.visibilityOverlayPipelineReady,
            traditionalReady);
    }

    bool RenderGeometryOutlineFeaturePass(
        const GFX::Context& graphics,
        const MeshPassResources& passResources) {

        GFX::PIX::ScopedGpuEvent event(
            graphics.cmdList,
            GFX::PIX::kColorRender,
            "RenderFeature.GeometryOutline");
        GFX::GPU_PROFILE::ScopedGpuTimer timer(
            graphics.cmdList,
            GFX::GPU_PROFILE::Pass::StylizedGeometryOutline);
        return ExecuteMaterialScopedFeaturePasses(
            graphics,
            passResources,
            MeshDrawPassKind::GeometryOutline,
            RENDER3D::MESHLET::MeshletPipelineKind::GeometryOutline);
    }

    bool RenderVisibilityOverlayFeaturePass(
        const GFX::Context& graphics,
        const MeshPassResources& passResources) {

        GFX::PIX::ScopedGpuEvent event(
            graphics.cmdList,
            GFX::PIX::kColorRender,
            "RenderFeature.VisibilityOverlay");
        GFX::GPU_PROFILE::ScopedGpuTimer timer(
            graphics.cmdList,
            GFX::GPU_PROFILE::Pass::StylizedVisibilityOverlay);
        return ExecuteMaterialScopedFeaturePasses(
            graphics,
            passResources,
            MeshDrawPassKind::VisibilityOverlay,
            RENDER3D::MESHLET::MeshletPipelineKind::VisibilityOverlay);
    }

    bool HasDepthPrepassWork() {
        return HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::DepthPrepass);
    }

    bool RenderDepthPrepass(
        const GFX::Context& graphics,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv) {
        return RenderDepthPrepassInternal(graphics, sceneDsv);
    }

    bool RenderWaterVisibilityDepth(
        const GFX::Context& graphics,
        D3D12_CPU_DESCRIPTOR_HANDLE waterDsv,
        const MeshPassResources& passResources) {
        return RenderWaterVisibilityDepthInternal(
            graphics,
            waterDsv,
            passResources);
    }

    bool FinalizeGpuDrivenVisibilityWithoutDepth(
        const GFX::Context& graphics) {
        if (!HasGpuDrivenSceneSource()) {
            return false;
        }

        GFX::PIX::ScopedGpuEvent pixFinalize(
            graphics.cmdList,
            GFX::PIX::kColorUpload,
            "GpuDepthVisibility.FinalizeVisibility.NoHZB");
        BuildGpuDrivenWorkFrame(
            graphics,
            nullptr,
            MakeMainCameraGpuDrivenPassMask(),
            true);
        SyncGpuDrivenBackendAvailability();
        BuildStrictGpuDrivenCommandFrame(graphics);
        return true;
    }

    bool FinalizeGpuDrivenVisibilityFromDepth(
        const GFX::Context& graphics,
        const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& depthVisibilityStats) {

        const HIKARI::RENDER3D::DEPTH::DepthPyramidView& depthPyramid =
            depthVisibilityStats.depthPyramid;
        if (!HasGpuDrivenSceneSource() ||
            !depthPyramid.valid ||
            depthPyramid.pyramidSrv.ptr == 0 ||
            depthPyramid.width == 0 ||
            depthPyramid.height == 0 ||
            !depthPyramid.viewProjValid) {
            return false;
        }

        GFX::PIX::ScopedGpuEvent pixFinalize(
            graphics.cmdList,
            GFX::PIX::kColorUpload,
            "GpuDepthVisibility.FinalizeVisibility");
        BuildGpuDrivenWorkFrame(
            graphics,
            &depthPyramid,
            MakeMainCameraGpuDrivenPassMask(),
            true);
        SyncGpuDrivenBackendAvailability();
        BuildStrictGpuDrivenCommandFrame(graphics);
        return true;
    }

    bool RenderForwardOpaquePass(
        const GFX::Context& graphics,
        const MeshPassResources& passResources) {
        if (!HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque)) {
            return true;
        }
        const GeometryBackendExecutionResult backendResult =
            ExecuteGeometryBackendPlan(
                graphics,
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque,
                passResources,
                MeshDrawPassKind::Forward,
                RENDER3D::MESHLET::MeshletPipelineKind::ForwardOpaque);
        return backendResult.gpuBackendExecuted;
    }

    bool RenderForwardTransparentPass(
        const GFX::Context& graphics,
        const MeshPassResources& passResources) {
        bool surfaceExecuted = true;
        if (HasGpuDrivenPassSource(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent)) {
            const GeometryBackendExecutionResult backendResult =
                ExecuteGeometryBackendPlan(
                    graphics,
                    RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent,
                    passResources,
                    MeshDrawPassKind::Forward,
                    RENDER3D::MESHLET::MeshletPipelineKind::ForwardTransparent);
            surfaceExecuted = backendResult.gpuBackendExecuted;
        }
        const MeshFrameResources& frame = GetActiveMeshFrameResources();
        const bool particlesExecuted =
            ExecutePreparedParticleForwardTransparent(
                graphics,
                frame.cameraCB != nullptr
                    ? frame.cameraCB->GetGPUVirtualAddress()
                    : 0u);
        return surfaceExecuted && particlesExecuted;
    }

    bool HasDepthAwarePassWork() {
        return HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware);
    }

    bool HasForwardTransparentPassWork() {
        return HasGpuDrivenPassSource(
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent) ||
            gMeshRendererState.particlePass.HasDrawWork();
    }

    bool RenderDepthAwarePass(
        const GFX::Context& graphics,
        const MeshPassResources& passResources) {
        if (!HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware)) {
            return true;
        }
        const GeometryBackendExecutionResult backendResult =
            ExecuteGeometryBackendPlan(
                graphics,
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware,
                passResources,
                MeshDrawPassKind::Forward,
                RENDER3D::MESHLET::MeshletPipelineKind::ForwardDepthAware);
        return backendResult.gpuBackendExecuted;
    }

    void SetAmbientOcclusionRuntimeEnabled(bool enabled) {
        SkyEnvironmentCB* skyEnvironment =
            GetActiveMeshFrameResources().skyEnvironmentMapped;
        if (skyEnvironment != nullptr) {
            skyEnvironment->ambientSamplingParams.x =
                enabled ? 1.0f : 0.0f;
        }
    }


} // namespace HIKARI::MESHRENDERER
