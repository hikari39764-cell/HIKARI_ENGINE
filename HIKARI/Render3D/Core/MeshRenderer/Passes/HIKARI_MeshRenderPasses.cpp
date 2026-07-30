#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <algorithm>

#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Core/MeshRenderer/Internal/HIKARI_MeshRendererInternal.h"
#include "Render3D/GpuDriven/HIKARI_GpuDepthVisibilityLayer.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceBuffer.h"

namespace HIKARI::MESHRENDERER::INTERNAL {
    bool RenderGeometryAuxPassInternal(
        const GFX::Context& graphics,
        RENDER3D::SURFACE::SceneSurfaceBuffer& surfaceBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv) {
        if (!HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardOpaque)) {
            return false;
        }

        const CameraCB* camera = GetActiveMeshFrameResources().cameraMapped;
        const uint32_t width = static_cast<uint32_t>(
            std::max(1.0f, camera ? camera->screenParams.x : 1.0f));
        const uint32_t height = static_cast<uint32_t>(
            std::max(1.0f, camera ? camera->screenParams.y : 1.0f));
        if (!surfaceBuffer.EnsureSize(graphics, width, height)) {
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
                MeshDrawPassKind::GeometryAux,
                RENDER3D::MESHLET::MeshletPipelineKind::GeometryAux);
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
            std::max(1.0f, camera ? camera->screenParams.x : 1.0f));
        const uint32_t height = static_cast<uint32_t>(
            std::max(1.0f, camera ? camera->screenParams.y : 1.0f));
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


} // namespace HIKARI::MESHRENDERER::INTERNAL

namespace HIKARI::MESHRENDERER {

    using namespace INTERNAL;
    bool RenderGeometryAuxPass(
        const GFX::Context& graphics,
        RENDER3D::SURFACE::SceneSurfaceBuffer& surfaceBuffer,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv) {
        return RenderGeometryAuxPassInternal(graphics, surfaceBuffer, sceneDsv);
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
        if (!HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent)) {
            return true;
        }
        const GeometryBackendExecutionResult backendResult =
            ExecuteGeometryBackendPlan(
                graphics,
                RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent,
                passResources,
                MeshDrawPassKind::Forward,
                RENDER3D::MESHLET::MeshletPipelineKind::ForwardTransparent);
        return backendResult.gpuBackendExecuted;
    }

    bool HasDepthAwarePassWork() {
        return HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardDepthAware);
    }

    bool HasForwardTransparentPassWork() {
        return HasGpuDrivenPassSource(
            RENDER3D::GPUDRIVEN::GpuDrivenPassKind::ForwardTransparent);
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
