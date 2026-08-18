#include "Render3D/Water/HIKARI_WaterVisibleDepth.h"

#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"
#include "Render3D/GpuDriven/HIKARI_GpuDepthVisibilityLayer.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::RENDER3D::WATER {

    namespace {
        struct WaterVisibleDepthState {
            GPUDRIVEN::GpuDepthVisibilityLayer layer{};
        };

        WaterVisibleDepthState& GetVisibleDepthState() {
            static WaterVisibleDepthState state{};
            return state;
        }
    }

    WaterVisibleDepthView RenderWaterVisibleDepth(
        const GFX::Context& graphics,
        const HIKARI::MESHRENDERER::MeshPassResources& passResources,
        uint32_t width,
        uint32_t height,
        uint64_t frameIndex,
        const ID3D12Resource* viewToken) {

        WaterVisibleDepthView result{};
        if (graphics.device == nullptr ||
            graphics.cmdList == nullptr ||
            viewToken == nullptr ||
            width == 0 ||
            height == 0 ||
            !passResources.sceneDepth.IsSampleable() ||
            passResources.sceneDepth.width != width ||
            passResources.sceneDepth.height != height) {
            return result;
        }

        UpdateRenderResourceDescriptorPoolContext(graphics);
        DEPTH::UpdateDepthPyramidContext(graphics);
        if (!MESHRENDERER::HasDepthAwarePassWork()) {
            return result;
        }

        WaterVisibleDepthState& state = GetVisibleDepthState();
        state.layer.ResetFrame();
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv =
            state.layer.BeginDepthPrepass(
                graphics,
                graphics.cmdList,
                width,
                height);
        if (dsv.ptr == 0) {
            return result;
        }

        GFX::PIX::ScopedGpuEvent event(
            graphics.cmdList,
            GFX::PIX::kColorRender,
            "Water.VisibleDepth");
        const bool drawExecuted = MESHRENDERER::RenderWaterVisibilityDepth(
            graphics,
            dsv,
            passResources);
        const bool depthReadable = state.layer.EndDepthPrepass(
            graphics,
            graphics.cmdList);
        if (!depthReadable || !drawExecuted) {
            return result;
        }

        const GPUDRIVEN::GpuDepthVisibilityStats& stats =
            state.layer.GetStats();
        if (!stats.visibilityDepthReady ||
            stats.visibilityDepthSrv.ptr == 0) {
            return result;
        }

        result.srv = stats.visibilityDepthSrv;
        result.width = width;
        result.height = height;
        // GFX::Context::frameIndex is the swap-chain resource slot.  The
        // volumetric consumer validates against the monotonic render-frame
        // serial, so the producer must preserve that explicit serial here.
        result.frameIndex = frameIndex;
        result.viewToken = viewToken;
        result.valid = true;
        return result;
    }

    void ShutdownWaterVisibleDepth() {
        GetVisibleDepthState().layer.Release();
    }

} // namespace HIKARI::RENDER3D::WATER
