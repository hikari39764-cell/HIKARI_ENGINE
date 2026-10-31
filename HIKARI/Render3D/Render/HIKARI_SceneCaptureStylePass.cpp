#include "Render3D/Render/HIKARI_SceneCaptureStylePass.h"

#include <algorithm>
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
                           const GameProjectSceneCaptureStyleDesc& style, const SceneCaptureStyleDepth& depth,
                           const std::vector<D3D12_RECT>* rects, const SceneCaptureUnstyledRegions* unstyled) {
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
    params.user[4] = {style.shadowOffsetX, style.shadowOffsetY, style.fillStrength, 0.0f};
    // 深度の効果（版 4）。深度が無い時は gUser[9].x = 0 でシェーダーが全部飛ばす。
    const bool hasDepth = depth.srv.ptr != 0u && style.depthFocus > 0.0f && depth.farZ > depth.nearZ;
    // 描き方を受けない面（版 7）。深度で面に乗る画素を見分けるので、深度が読める時だけ。
    const bool depthBound = depth.srv.ptr != 0u && depth.farZ > depth.nearZ;
    const uint32_t unstyledCount =
        depthBound && unstyled != nullptr ? (std::min)(unstyled->count, kSceneCaptureUnstyledRegionCount) : 0u;
    params.user[5] = {style.depthFocus, style.backgroundDepth, style.fogStrength, style.fogDepth};
    params.user[6] = {style.fogColor.x, style.fogColor.y, style.fogColor.z, style.occlusion};
    params.user[7] = {style.occlusionRadius, style.occlusionDepth, style.depthLine, style.lightShadow};
    params.user[8] = {style.lightSlope, style.lightReach, depth.nearZ, depth.farZ};
    params.user[9] = {hasDepth ? 1.0f : 0.0f, depth.orthographic ? 1.0f : 0.0f, style.depthLineGap, style.transparentBackground};
    // 色調（版 6）：鮮やかな物体の外だけに掛ける。
    params.user[10] = {style.toneTint.x, style.toneTint.y, style.toneTint.z, style.toneTintStrength};
    params.user[11] = {style.toneHue, style.toneSaturation, style.toneBrightness, style.toneContrast};
    // 描き方を受けない面：[12]・[14] が矩形、[13]・[15] の xyz が奥行きの式。[13].w は面の数、[15].w は奥行きの許し
    // （視線方向の距離に対する割合。面の上の画素は誤差ほどしか離れず、手前の人物などは 1 割ほど近い）。
    for (uint32_t index = 0u; index < unstyledCount; ++index) {
        const SceneCaptureUnstyledRegion& region = unstyled->regions[index];
        params.user[12u + index * 2u] = {region.left, region.top, region.right, region.bottom};
        params.user[13u + index * 2u] = {region.a, region.b, region.c, 0.0f};
    }
    params.user[13].w = static_cast<float>(unstyledCount);
    params.user[15].w = 0.004f;
    state.effect->ApplyCommonParams(params);
    state.quad.SetInputTexture(input.GetSrvHeap(), input.GetSrvGpu(),
                               hasDepth || unstyledCount > 0u ? depth.srv : D3D12_GPU_DESCRIPTOR_HANDLE{});
    if (rects == nullptr) {
        output.BeginCapture();
        const bool drawn = state.effect->BindAndDraw(state.quad);
        output.EndCapture();
        return drawn;
    }
    // 矩形ごと：視口は全面のまま（四角の UV と texel の位置を像全体で合わせる）、切り抜きだけをその矩形へ絞る。
    output.Rebind();
    const bool drawn = state.effect->BindAndDrawScissored(state.quad, *rects);
    output.ResetViewport();
    output.Rebind();
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
