#pragma once
#include <vector>
#include <string>

#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Vfx/Post/HIKARI_PostCommon.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"

namespace HIKARI {
namespace POST {

class QuadDrawer;
class PostEffect;

class PostChain {
public:
    void Clear();
    void Add(PostEffect* effect);
    void UpdateContext(const GFX::Context& ctx);
    void SetDebugName(std::string name);
    std::string DumpState() const;

    bool HasAny() const { return !effects_.empty(); }
    // tempWidth/tempHeight を指定すると ping/pong をそのサイズで確保し、
    // 先頭エフェクトの描画がそのままダウンサンプルを兼ねる (bloom 用)。
    // 0 のときは src と同サイズ。
    RenderTarget2D* Execute(RenderTarget2D& src, QuadDrawer& quad, const CommonParams& params, int tempWidth = 0,
                            int tempHeight = 0, const RENDER3D::RenderTextureView* surfaceFeatureMask = nullptr);
    RenderTarget2D* GetPing() { return &ping_; }

    void Finalize();

private:
    bool EnsureTempSize(int w, int h);

private:
    std::vector<PostEffect*> effects_;

    RenderTarget2D ping_{};
    RenderTarget2D pong_{};
    bool tempsInitialized_ = false;
    std::string debugName_ = "PostChain";
    GFX::Context context_{};
};

} // namespace POST
} // namespace HIKARI
