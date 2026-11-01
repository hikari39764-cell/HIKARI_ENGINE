#pragma once

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenReadability.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI::RENDER3D::TEMPORAL {

// sceneCaptureSurfaces は場景キャプチャを映す面（画面座標の四隅）。面の動きベクトルは映る中身の動きを表さないので、
// その画素は reactive と履歴偏り（DLSS の BiasCurrentColor）で履歴を弱め、中身の残像を防ぐ。
bool ExecuteTemporalMaskPass(const GFX::Context& graphics, const TemporalInputs& inputs,
                             const ScreenReadabilityFrame& sceneCaptureSurfaces = {});
void ShutdownTemporalMaskPass();

} // namespace HIKARI::RENDER3D::TEMPORAL
