#include "Vfx/Post/HIKARI_GameUiCompositionStage.h"

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Render2D/HIKARI_DxRenderer.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"

#include <array>

namespace HIKARI::POST {

    namespace {
        constexpr DXGI_FORMAT kPresentationFormat =
            DXGI_FORMAT_R8G8B8A8_UNORM;
    }

    void GameUiCompositionStage::UpdateContext(const GFX::Context& context) {
        context_ = context;
        hudlessColor_.UpdateContext(context);
        uiColorAndAlpha_.UpdateContext(context);
        finalColor_.UpdateContext(context);
    }

    void GameUiCompositionStage::Shutdown() {
        if (active_) {
            uiColorAndAlpha_.EndCapture();
        }
        hudlessColor_.Finalize();
        uiColorAndAlpha_.Finalize();
        finalColor_.Finalize();
        active_ = false;
        InvalidateFrame();
    }

    bool GameUiCompositionStage::EnsureTargets(int width, int height) {
        if (width <= 0 || height <= 0) {
            return false;
        }

        auto ensureTarget = [width, height](
            RenderTarget2D& target,
            const char* debugName,
            const std::array<float, 4>& clearColor) {

            const bool invalid =
                !target.IsInitialized() ||
                target.GetWidth() != width ||
                target.GetHeight() != height ||
                target.GetFormat() != kPresentationFormat ||
                target.HasDepth();
            if (!invalid) {
                return true;
            }

            target.Finalize();
            target.SetDebugName(debugName);
            return target.Init(
                width,
                height,
                kPresentationFormat,
                false,
                clearColor);
        };

        return
            ensureTarget(
                uiColorAndAlpha_,
                "Presentation.UiColorAndAlpha",
                { 0.0f, 0.0f, 0.0f, 0.0f }) &&
            ensureTarget(
                finalColor_,
                "Presentation.FinalColor",
                { 0.0f, 0.0f, 0.0f, 1.0f });
    }

    bool GameUiCompositionStage::EnsureHudlessFallback(
        int width,
        int height) {

        const bool invalid =
            !hudlessColor_.IsInitialized() ||
            hudlessColor_.GetWidth() != width ||
            hudlessColor_.GetHeight() != height ||
            hudlessColor_.GetFormat() != kPresentationFormat ||
            hudlessColor_.HasDepth();
        if (!invalid) {
            return true;
        }

        hudlessColor_.Finalize();
        hudlessColor_.SetDebugName("Presentation.HudlessFallback");
        return hudlessColor_.Init(
            width,
            height,
            kPresentationFormat,
            false,
            { 0.0f, 0.0f, 0.0f, 1.0f });
    }

    void GameUiCompositionStage::InvalidateFrame() {
        frame_ = {};
    }

    bool GameUiCompositionStage::Begin(
        RenderTarget2D& sceneColor,
        QuadDrawer& quad,
        int outputWidth,
        int outputHeight,
        int logicalWidth,
        int logicalHeight,
        uint64_t frameIndex) {

        if (active_) {
            uiColorAndAlpha_.EndCapture();
            active_ = false;
        }
        InvalidateFrame();

        const bool canAliasHudless =
            sceneColor.GetWidth() == outputWidth &&
            sceneColor.GetHeight() == outputHeight &&
            sceneColor.GetFormat() == kPresentationFormat;
        if (sceneColor.GetResource() == nullptr ||
            !EnsureTargets(outputWidth, outputHeight) ||
            (!canAliasHudless &&
                !EnsureHudlessFallback(outputWidth, outputHeight)) ||
            !quad.SetOutputFormat(kPresentationFormat)) {
            DEBUGLOG::PushRenderError(
                "[GameUiCompositionStage][ERROR] Unable to prepare presentation targets.");
            return false;
        }

        RenderTarget2D* hudless = &sceneColor;
        if (!canAliasHudless) {
            hudlessColor_.BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
            quad.DrawFullscreen(
                sceneColor.GetSrvHeap(),
                sceneColor.GetSrvGpu());
            hudlessColor_.EndCapture();
            hudless = &hudlessColor_;
        }

        uiColorAndAlpha_.BeginCapture(0.0f, 0.0f, 0.0f, 0.0f);
        if (!DX::DxRenderer::SetOutputTarget(
                kPresentationFormat,
                logicalWidth,
                logicalHeight)) {
            uiColorAndAlpha_.EndCapture();
            DEBUGLOG::PushRenderError(
                "[GameUiCompositionStage][ERROR] Render2D LDR output pipeline is unavailable.");
            return false;
        }

        frame_.frameIndex = frameIndex;
        frame_.width = outputWidth;
        frame_.height = outputHeight;
        frame_.uiLogicalWidth = logicalWidth;
        frame_.uiLogicalHeight = logicalHeight;
        frame_.format = kPresentationFormat;
        frame_.hudlessColor = hudless;
        frame_.uiColorAndAlpha = &uiColorAndAlpha_;
        frame_.hudlessReady = true;
        active_ = true;
        return true;
    }

    RenderTarget2D* GameUiCompositionStage::End(QuadDrawer& quad) {
        if (!active_) {
            return nullptr;
        }

        uiColorAndAlpha_.EndCapture();
        active_ = false;
        frame_.uiReady = true;

        if (!quad.SetOutputFormat(kPresentationFormat)) {
            InvalidateFrame();
            return nullptr;
        }

        finalColor_.BeginCapture(0.0f, 0.0f, 0.0f, 1.0f);
        quad.DrawFullscreen(
            frame_.hudlessColor->GetSrvHeap(),
            frame_.hudlessColor->GetSrvGpu());
        quad.DrawBlended(
            uiColorAndAlpha_.GetSrvHeap(),
            uiColorAndAlpha_.GetSrvGpu(),
            BlendOption::PremultipliedAlpha);
        finalColor_.EndCapture();

        frame_.finalColor = &finalColor_;
        frame_.finalReady = true;
        return &finalColor_;
    }

} // namespace HIKARI::POST
