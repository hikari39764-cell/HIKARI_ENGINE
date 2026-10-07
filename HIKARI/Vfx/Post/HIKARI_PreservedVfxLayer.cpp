#include "Vfx/Post/HIKARI_PreservedVfxLayer.h"
#include "Core/HIKARI_TimeService.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"

namespace HIKARI::POST {

bool PreservedVfxLayer::EnsureTarget(RenderTarget2D& target, int width, int height) {
    target.UpdateContext(context_);
    if (target.GetWidth() == width && target.GetHeight() == height && target.IsInitialized())
        return true;
    target.Finalize();
    if (!target.Init(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT))
        return false;
    target.SetDebugName("Vfx.PreservedColor");
    return true;
}

bool PreservedVfxLayer::Begin(const RENDER3D::PIPELINE::RenderFrameContext& frame,
                              const RENDER3D::TEMPORAL::TemporalFrameState& temporal,
                              RENDER3D::PIPELINE::RenderTargetAccess& target) {
    target = {};
    if (active_ || ready_ || !frame.surfaceReceipt.IsValid() || !frame.surfaceReceipt.frame.Matches(frame.token) ||
        !temporal.publication.Matches(frame.token) || !temporal.view.IsValid() ||
        temporal.view.viewId != frame.token.viewId || !temporal.camera.valid || temporal.aborted ||
        frame.token.frameIndex != TIME::GetFrameContext().frameIndex ||
        frame.graphics.deviceGeneration != frame.surfaceReceipt.deviceGeneration || !frame.graphics.device ||
        !frame.graphics.cmdList || !frame.scene.readOnlyDsv.ptr || !frame.scene.depth.IsSampleable() ||
        frame.renderWidth != frame.scene.depth.width || frame.renderHeight != frame.scene.depth.height ||
        frame.renderWidth != temporal.renderWidth || frame.renderHeight != temporal.renderHeight ||
        !MATH::IsFinite(temporal.camera.jitter) || !RENDER3D::SURFACE::TryGetRenderFrameSurfaces(frame.surfaceReceipt))
        return false;
    if (context_.device &&
        (context_.device != frame.graphics.device || context_.deviceGeneration != frame.graphics.deviceGeneration))
        Shutdown();
    context_ = frame.graphics;
    if (!EnsureTarget(capture_, static_cast<int>(frame.renderWidth), static_cast<int>(frame.renderHeight)))
        return false;
    receipt_ = frame.surfaceReceipt;
    sceneTarget_ = frame.targetAccess;
    depth_ = frame.scene.readOnlyDsv;
    jitterPixels_ = {temporal.camera.jitter.x, temporal.camera.jitter.y};
    capture_.BeginCapture(0, 0, 0, 0);
    active_ = true;
    if (!Rebind()) {
        (void)End(false);
        return false;
    }
    target.userData = this;
    target.rebind = [](void* self) { return static_cast<PreservedVfxLayer*>(self)->Rebind(); };
    target.beginDepthRead = target.rebind;
    return true;
}

bool PreservedVfxLayer::Rebind() {
    if (!active_ || !sceneTarget_.BeginDepthRead())
        return false;
    capture_.Rebind();
    const auto rtv = capture_.GetRtvHandle();
    context_.cmdList->OMSetRenderTargets(1, &rtv, FALSE, &depth_);
    return true;
}

bool PreservedVfxLayer::End(bool succeeded) {
    if (!active_)
        return false;
    capture_.EndCapture();
    const bool restored = sceneTarget_.Rebind() && sceneTarget_.BeginDepthRead();
    active_ = false;
    ready_ = succeeded && restored;
    sceneTarget_ = {};
    return restored;
}

bool PreservedVfxLayer::Resolve(RENDER3D::SURFACE::RenderFrameSurfaceReceipt receipt, MATH::Vec2 outputJitterPixels,
                                int width, int height, QuadDrawer& quad, RenderTarget2D*& output) {
    output = nullptr;
    if (!ready_)
        return true;
    if (!receipt_.Matches(receipt) || receipt.frame.frameIndex != TIME::GetFrameContext().frameIndex ||
        receipt.deviceGeneration != context_.deviceGeneration || width <= 0 || height <= 0 ||
        !MATH::IsFinite(outputJitterPixels) || !RENDER3D::SURFACE::TryGetRenderFrameSurfaces(receipt))
        return false;
    ready_ = false; // exactly one HDR composite per capture
    const MATH::Vec2 offset{(jitterPixels_.x - outputJitterPixels.x) / capture_.GetWidth(),
                            (jitterPixels_.y - outputJitterPixels.y) / capture_.GetHeight()};
    if (offset.x == 0 && offset.y == 0) {
        output = &capture_; // normalized UV sampling also handles render/output scaling
        return true;
    }
    if (!resolveEffect_) {
        auto effect = std::make_unique<PostEffect>();
        PostEffect::UpdateContext(context_);
        if (!effect->LoadPixelShader({.programId = "HIKARI.EngineShader.Post_VfxResolvePS"}))
            return false;
        resolveEffect_ = std::move(effect);
    }
    if (!EnsureTarget(resolved_, width, height) || !quad.SetOutputFormat(resolved_.GetFormat()))
        return false;
    resolved_.Rebind();
    PostEffect::UpdateContext(context_);
    CommonParams params{};
    params.resolutionX = static_cast<float>(width);
    params.resolutionY = static_cast<float>(height);
    params.user[0] = {offset.x, offset.y, 0, 0};
    resolveEffect_->ApplyCommonParams(params);
    quad.SetInputTexture(capture_.GetSrvHeap(), capture_.GetSrvGpu());
    const bool drawn = resolveEffect_->BindAndDraw(quad);
    resolved_.EndCapture();
    output = drawn ? &resolved_ : nullptr;
    return drawn;
}

void PreservedVfxLayer::Invalidate() noexcept {
    receipt_ = {};
    sceneTarget_ = {};
    active_ = ready_ = false;
}

void PreservedVfxLayer::Shutdown() {
    Invalidate();
    capture_.Finalize();
    resolved_.Finalize();
    resolveEffect_.reset();
    context_ = {};
}

} // namespace HIKARI::POST
