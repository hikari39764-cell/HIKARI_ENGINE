#pragma once

#include "Gfx/HIKARI_GfxContext.h"
#include "RenderCore/Targets/HIKARI_RenderTarget2D.h"
#include "Vfx/Post/HIKARI_PresentationFrame.h"
#include "Vfx/Post/HIKARI_GameUiBackdropComposite.h"

namespace HIKARI::POST {

class QuadDrawer;

class GameUiCompositionStage final {
public:
    void UpdateContext(const GFX::Context& context);
    void Shutdown();

    bool Begin(RenderTarget2D& sceneColor, QuadDrawer& quad, int outputWidth, int outputHeight, int logicalWidth,
               int logicalHeight, uint64_t frameIndex, bool fitScene);

    RenderTarget2D* End(QuadDrawer& quad);
    bool IsActive() const { return active_; }
    const PresentationFrameResources& GetFrameResources() const { return frame_; }

private:
    bool EnsureTargets(int width, int height);
    bool EnsureHudlessFallback(int width, int height);
    void InvalidateFrame();

    GFX::Context context_{};
    RenderTarget2D hudlessColor_{};
    RenderTarget2D uiColorAndAlpha_{};
    RenderTarget2D dialogueBackdropColor_{};
    RenderTarget2D finalColor_{};
    GameUiBackdropComposite backdropComposite_{};
    PresentationFrameResources frame_{};
    bool active_ = false;
};

} // namespace HIKARI::POST
