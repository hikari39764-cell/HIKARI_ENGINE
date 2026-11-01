#pragma once

#include <cstdint>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_CameraOptics.h"
#include "RenderCore/Resources/HIKARI_RenderTextureView.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

namespace HIKARI {
class RenderTarget2D;
}

namespace HIKARI::RENDER3D::TEMPORAL {
struct TemporalFrameState;
}

// カメラの運動ブラー。TAA の後の出力解像度のシーン線形 HDR に、描画解像度の運動ベクトルから求めた速度で掛ける。
namespace HIKARI::RENDER3D::MOTIONBLUR {

// 描画に必要な入力。運動ベクトルと深度は描画解像度、色は出力解像度。
struct CameraMotionBlurInputs {
    RenderTarget2D* sceneColor = nullptr;
    // 時域資源が持つ運動ベクトル（借り物。読む間だけ計算 shader からも読める状態にして戻す）。
    RenderTextureView motionVectors{};
    // 深度を持つ場面の描画目標（深度の読み取りの状態遷移はこの所有者が行う）。
    RenderTarget2D* sceneDepthOwner = nullptr;
    const TEMPORAL::TemporalFrameState* temporalFrame = nullptr;
    CAMERA::MotionBlurSettings settings{};
    MotionBlurQuality quality = MotionBlurQuality::Off;
    bool orthographic = false;
};

// ブラーを掛けた目標を返す。無効・入力不足・失敗なら sceneColor のまま。
RenderTarget2D* ExecuteCameraMotionBlur(const GFX::Context& context, const CameraMotionBlurInputs& inputs);
void ShutdownCameraMotionBlurStage() noexcept;

} // namespace HIKARI::RENDER3D::MOTIONBLUR
