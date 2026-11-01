#pragma once

#include "Core/Math/HIKARI_Math3D.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenComposite.h"

namespace HIKARI {
class RenderTarget2D;
}

namespace HIKARI::RENDER3D::TEMPORAL {

// 場景キャプチャを映す画面を、時間方向の拡大の後に出力の解像度で貼り直す（HIKARI_ScreenComposite.h）。
// output は拡大の結果（出力の解像度、前露出の HDR）、depthSrv は同じ大きさの時間方向に均した深度（R32_FLOAT、
// 共有の記述子ヒープ上）。viewExposure は {P, 1/P, P/E, P/P_prev}、霧は主ビューの前向きの解析霧の定数。
// 貼る画面が無い・準備できない時は何もせず false（拡大の結果がそのまま残る）。
bool ExecuteTemporalScreenComposite(const GFX::Context& graphics, RenderTarget2D& output,
                                    D3D12_GPU_DESCRIPTOR_HANDLE depthSrv, const ScreenCompositeFrame& frame,
                                    const MATH::Vec4& viewExposure, const MATH::Vec4& fogParams,
                                    const MATH::Vec4& fogColorDensity);
void ShutdownTemporalScreenCompositePass();

} // namespace HIKARI::RENDER3D::TEMPORAL
