#include "Render3D/Render/HIKARI_SceneCaptureStylePass.h"

#include <memory>
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "Vfx/Post/HIKARI_PostEffect.h"
#include "Vfx/Post/HIKARI_PostQuadDrawer.h"

namespace HIKARI::RENDER3D {
namespace {
struct CaptureStylePass {
    POST::QuadDrawer quad;
    std::unique_ptr<POST::PostEffect> effect;
    uint64_t deviceGeneration = 0u;
};

CaptureStylePass& State() {
    static CaptureStylePass state;
    return state;
}
} // namespace

bool DrawSceneCaptureStyle(const GFX::Context& graphics, const RenderTarget2D& input, RenderTarget2D& output,
                           const GameProjectSceneCaptureStyleDesc& style) {
    auto& state = State();
    if (graphics.device == nullptr || graphics.cmdList == nullptr || !input.IsInitialized() ||
        !output.IsInitialized() || input.GetResource() == output.GetResource() ||
        (input.GetColorState() & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) == 0u) {
        return false;
    }
    if (state.deviceGeneration != graphics.deviceGeneration) {
        ResetSceneCaptureStylePass();
        state.deviceGeneration = graphics.deviceGeneration;
    }
    if (!state.quad.Init(graphics) || !state.quad.SetOutputFormat(output.GetFormat())) {
        return false;
    }
    POST::PostEffect::UpdateContext(graphics);
    if (!state.effect) {
        auto effect = std::make_unique<POST::PostEffect>();
        if (!effect->LoadPixelShader({.programId = "HIKARI.EngineShader.Post_SceneCaptureStylePS"})) {
            return false;
        }
        state.effect = std::move(effect);
    }

    // シェーダーの gUser と一対一。長さはゲーム画素、色は表示 sRGB のまま渡しシェーダーで線形へ直す。
    POST::CommonParams params{};
    params.intensity = 1.0f;
    params.resolutionX = static_cast<float>(input.GetWidth());
    params.resolutionY = static_cast<float>(input.GetHeight());
    params.user[0] = {static_cast<float>(style.style), style.pixelSize, style.lineWidth, style.shadowStrength};
    params.user[1] = {style.lineColor.x, style.lineColor.y, style.lineColor.z, style.brightness};
    params.user[2] = {style.paperColor.x, style.paperColor.y, style.paperColor.z,
                      1.0f / COLOR::GetActiveViewExposure().z};
    params.user[3] = {style.accentColor.x, style.accentColor.y, style.accentColor.z, style.backgroundLuminance};
    params.user[4] = {style.shadowOffsetX, style.shadowOffsetY, 0.0f, 0.0f};
    state.effect->ApplyCommonParams(params);
    state.quad.SetInputTexture(input.GetSrvHeap(), input.GetSrvGpu());
    output.BeginCapture();
    const bool drawn = state.effect->BindAndDraw(state.quad);
    output.EndCapture();
    return drawn;
}

void ResetSceneCaptureStylePass() noexcept {
    auto& state = State();
    state.effect.reset();
    state.quad.Finalize();
    state.deviceGeneration = 0u;
}
} // namespace HIKARI::RENDER3D
