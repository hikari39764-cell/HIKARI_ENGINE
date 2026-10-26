#pragma once

#include "Gfx/HIKARI_GfxContext.h"
#include "HIKARI/Project/SceneCaptureTypes.h"
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

// 入力は前露出 HDR、出力はスクリーンの Unlit 材質が読む表示参照の線形色。
// rects を渡すと出力を消さず、その矩形の中だけを描く（残りは前のフレームの絵のまま）。空なら何も描かない。
bool DrawSceneCaptureStyle(const GFX::Context& graphics, const RenderTarget2D& input, RenderTarget2D& output,
                           const GameProjectSceneCaptureStyleDesc& style, const SceneCaptureStyleDepth& depth = {},
                           const std::vector<D3D12_RECT>* rects = nullptr);
void ResetSceneCaptureStylePass() noexcept;

} // namespace HIKARI::RENDER3D
