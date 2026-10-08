#pragma once

#include <cstdint>
#include <limits>

#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI::RENDER3D::COLOR {

enum class ExposureMode : uint8_t {
    Manual = 0u,
    Automatic = 1u,
};

inline constexpr TEXT::EnumName<ExposureMode> kExposureModeNames[] = {
    {ExposureMode::Manual, "Manual"},
    {ExposureMode::Automatic, "Automatic"},
};

// 露出の設定。EV100 は ISO 100 換算の露出値（晴天の屋外 約 15、明るい室内 約 7、月夜 約 -2）。
struct ExposureSettings {
    ExposureMode mode = ExposureMode::Automatic;
    float manualEv100 = 14.0f;
    // 正で明るくする補正 [EV]。手動・自動の両方に掛かる。
    float compensationEv = 0.0f;
    // 自動露出が取れる EV100 の範囲と、明るい側・暗い側への順応速度 [1/s]。
    float minEv100 = -10.0f;
    float maxEv100 = 18.0f;
    // 暗所の順応。測った EV100 が nightAdaptationEv100 より低い分のうち strength の割合だけ露出を上げずに残し、
    // 夜を夜らしく暗く見せる（満月の場面 EV100 -3 は約 2.8 段暗く、星明かりは星と銀河が見える程度）。
    float nightAdaptationEv100 = 5.0f;
    float nightAdaptationStrength = 0.35f;
    // 明部の保護。画面の上位 5% の明るさが中間灰の 3.5 段上を超える分のうち、この割合だけ露出を下げる
    // （夕日の空の色を白く飛ばさず、前景は少し暗くなる。カメラの評価測光に近い）。
    float highlightProtection = 0.5f;
    // 直射日光の下で、反射式の測光が入射照度の露出（晴天の正しい露出、いわゆる sunny 16）より暗くする分を、
    // この段数まで戻す（空が画の大半を占めても雲が灰色にならない。カメラの評価測光の空の補正に近い）。
    float sunlitMeteringCorrectionEv = 1.0f;
    // 昼の光（入射 EV100 15）より暗い天気（曇り・雨・薄暮）で、照度の差のうちこの割合だけ露出を上げずに残す
    // （曇天はおよそ 0.5 段、暴風雨はおよそ 1 段暗い）。夜が近づくと暗所の順応へ移る。
    float dimAdaptationStrength = 0.3f;
    float speedUp = 3.0f;
    float speedDown = 1.0f;

    bool operator==(const ExposureSettings&) const = default;
};

// 場面を照らす光の量。反射式の測光（画面の平均輝度）は空の多い画で暗く、暗い天気も中間灰へ戻すので、
// 入射式露出計と同じく場面を照らす光そのもので補う。
struct ExposureIllumination {
    // 水平面の照度の EV100（入射式露出計、平板の較正 C = 250）。晴天の昼はおよそ 15.5。NaN なら補わない。
    float incidentEv100 = std::numeric_limits<float>::quiet_NaN();
    // 直射日光が場面を照らしている度合い（0..1）。雲を抜ける割合 × 太陽の高さの重み。
    float directSun = 0.0f;
};

// 反射式露出計の較正（K = 12.5）。その EV100 で測れる平均輝度を中間灰 0.18 へ写す露出 E。
float ExposureFromEv100(float ev100) noexcept;
float Ev100FromExposure(float exposure) noexcept;
// 手動露出の E（補正込み）。
float ResolveManualExposure(const ExposureSettings& settings) noexcept;

// Scene-linear HDR から表示変換までのフレーム露出契約。Temporal、Upscaler、Post はこの契約だけを参照する。
// exposure (E) は絶対シーン輝度を表示参照値（中間グレー 0.18）へ写す係数。
// preExposure (P) はこのフレームの HDR 目標へ書く前に掛けた係数で、後処理は残差 E/P だけを掛ける。
// 跨フレームの履歴は P/P_prev で現在の空間へ換算する。
struct ExposureFrameState {
    ExposureMode mode = ExposureMode::Manual;
    float exposure = 1.0f;
    float preExposure = 1.0f;
    float previousPreExposure = 1.0f;
    float meteredLuminance = 0.18f;
    float targetExposure = 1.0f;
    bool valid = true;
    bool meteringValid = false;
};

struct DisplayColorFrameState {
    float whiteBalanceR = 1.0f;
    float whiteBalanceG = 1.0f;
    float whiteBalanceB = 1.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    bool valid = true;
};

// previousPreExposure は直前フレームの P。初回は 0 以下を渡すと今回の P を使う。
ExposureFrameState BuildManualExposureFrame(float sceneExposure, float previousPreExposure, float viewExposure = 1.0f);
// highlightLuminance は画面の上位 5% の明るさ [nits]（明部の保護に使う）。
ExposureFrameState BuildAutomaticExposureFrame(const ExposureSettings& settings,
                                               const ExposureIllumination& illumination, float measuredLuminance,
                                               float highlightLuminance, float previousExposure,
                                               float previousPreExposure, float deltaTime, bool measurementValid);
DisplayColorFrameState BuildDisplayColorFrame(float whiteBalanceTemperature, float whiteBalanceTint, float contrast,
                                              float saturation);
// 時域入力へ渡す絶対露出 E（不正な状態では 1）。
float ResolveTemporalExposure(const ExposureFrameState& state) noexcept;
// 後処理が掛ける残差 E/P。
float ResidualExposure(const ExposureFrameState& state) noexcept;
// 前フレームの履歴を現在の Pre-Exposure 空間へ換算する係数 P/P_prev。
float PreExposureRatio(const ExposureFrameState& state) noexcept;

} // namespace HIKARI::RENDER3D::COLOR
