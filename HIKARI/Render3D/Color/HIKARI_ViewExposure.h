#pragma once

#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Color/HIKARI_ExposureState.h"

// 描画中のビューが HDR 目標へ書く時の露出係数。HLSL 側は Include/Color/HIKARI_ViewExposure.hlsli。
// 物理量（nits）は x = P を掛けて書き、表示参照の色（1.0 = 露出後の白）は z = P/E を掛けて書く。
// 前フレームの履歴は w = P/P_prev で今の空間へ換算する。
namespace HIKARI::RENDER3D::COLOR {

// float4 {preExposure, 1 / preExposure, displayToScene, historyRatio}
MATH::Vec4 MakeViewExposureConstants(const ExposureFrameState& state) noexcept;

// ビューの描画区間に露出を設定する。入れ子にでき、抜ける時に外側の値へ戻す。
// 各 pass の定数は GetActiveViewExposure() から取る。区間外は P = 1（探針の捕獲などの絶対値）。
class ScopedViewExposure {
public:
    explicit ScopedViewExposure(const ExposureFrameState& state) noexcept;
    ~ScopedViewExposure();
    ScopedViewExposure(const ScopedViewExposure&) = delete;
    ScopedViewExposure& operator=(const ScopedViewExposure&) = delete;

private:
    MATH::Vec4 previous_{};
};

const MATH::Vec4& GetActiveViewExposure() noexcept;

} // namespace HIKARI::RENDER3D::COLOR
