#pragma once

#include <memory>
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Vfx/Post/HIKARI_PostEffect.h"

namespace HIKARI::POST {

// Optional frame-local PMA layer. It owns no camera, exposure, simulation, or history.
class PreservedVfxLayer {
public:
    bool Begin(const RENDER3D::PIPELINE::RenderFrameContext& frame,
               const RENDER3D::TEMPORAL::TemporalFrameState& temporal, RENDER3D::PIPELINE::RenderTargetAccess& target);
    bool End(bool succeeded);
    bool Resolve(RENDER3D::SURFACE::RenderFrameSurfaceReceipt receipt, MATH::Vec2 outputJitterPixels, int outputWidth,
                 int outputHeight, QuadDrawer& quad, RenderTarget2D*& output);
    void Invalidate() noexcept;
    void Shutdown();

private:
    bool Rebind();
    bool EnsureTarget(RenderTarget2D& target, int width, int height);
    GFX::Context context_{};
    RenderTarget2D capture_;
    RenderTarget2D resolved_;
    std::unique_ptr<PostEffect> resolveEffect_;
    RENDER3D::SURFACE::RenderFrameSurfaceReceipt receipt_{};
    RENDER3D::PIPELINE::RenderTargetAccess sceneTarget_{};
    D3D12_CPU_DESCRIPTOR_HANDLE depth_{};
    MATH::Vec2 jitterPixels_{};
    bool active_ = false;
    bool ready_ = false;
};

} // namespace HIKARI::POST
