#pragma once

#include "Gfx/HIKARI_GfxContext.h"
#include "HIKARI/Project/SceneCaptureTypes.h"
#include <array>
#include <cstdint>
#include <vector>

namespace HIKARI {
class RenderTarget2D;
}

namespace HIKARI::RENDER3D {

// キャプチャの深度（版 4 の深度の効果に使う）。srv が無ければ深度の効果は掛からない。
// srv は入力の色と同じ記述子ヒープにあり、呼び手が描く間だけ読める状態にしておく。
struct SceneCaptureStyleDepth {
    D3D12_GPU_DESCRIPTOR_HANDLE srv{};
    float nearZ = 0.0f;
    float farZ = 0.0f;
    bool orthographic = false;
};

// 描き方を受けない面（結び付けに Unstyled の付いた平らな面）がキャプチャに写った所。texel の外接矩形と、その面の上の
// 奥行きの式 a·x + b·y + c（x, y は texel。透視なら 1 / 視線方向の距離、正投影なら視線方向の距離で、平らな面では
// どちらも texel の位置の一次式になる）。シェーダーは矩形の中で深度がこの面に乗る画素だけを、描き方を掛けずに出す。
struct SceneCaptureUnstyledRegion {
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    float a = 0.0f, b = 0.0f, c = 0.0f;
};
// シェーダーの定数の空き（gUser[12]〜[15]）に収まる数。
inline constexpr uint32_t kSceneCaptureUnstyledRegionCount = 2u;
struct SceneCaptureUnstyledRegions {
    uint32_t count = 0u;
    std::array<SceneCaptureUnstyledRegion, kSceneCaptureUnstyledRegionCount> regions{};
};

// 入力は前露出 HDR、出力はスクリーンの Unlit 材質が読む表示参照の線形色。
// rects を渡すと出力を消さず、その矩形の中だけを描く（残りは前のフレームの絵のまま）。空なら何も描かない。
// unstyled の面は深度が要る（depth.srv が無ければ面は無いものとして普段どおり描く）。
bool DrawSceneCaptureStyle(const GFX::Context& graphics, const RenderTarget2D& input, RenderTarget2D& output,
                           const GameProjectSceneCaptureStyleDesc& style, const SceneCaptureStyleDepth& depth = {},
                           const std::vector<D3D12_RECT>* rects = nullptr,
                           const SceneCaptureUnstyledRegions* unstyled = nullptr);
void ResetSceneCaptureStylePass() noexcept;

} // namespace HIKARI::RENDER3D
