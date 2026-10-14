#pragma once

#include "Gfx/HIKARI_GfxContext.h"
#include "HIKARI/Project/SceneCaptureTypes.h"

namespace HIKARI {
class RenderTarget2D;
}

namespace HIKARI::RENDER3D {

// 入力は前露出 HDR、出力はスクリーンの Unlit 材質が読む表示参照の線形色。
bool DrawSceneCaptureStyle(const GFX::Context& graphics, const RenderTarget2D& input, RenderTarget2D& output,
                           const GameProjectSceneCaptureStyleDesc& style);
void ResetSceneCaptureStylePass() noexcept;

} // namespace HIKARI::RENDER3D
