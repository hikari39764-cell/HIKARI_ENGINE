#include "Render3D/Color/HIKARI_ExposureState.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::RENDER3D::COLOR {

namespace {
float SanitizePositive(float value, float fallback) noexcept {
    return std::isfinite(value) && value > 0.0f ? value : fallback;
}

// 露出係数の log2 の有効範囲。物理単位（EV100 -6..18 は log2 E 約 -17.5..6.5）を含める。
constexpr float kMinimumLog2Exposure = -24.0f;
constexpr float kMaximumLog2Exposure = 24.0f;
// 中間灰 0.18 と反射式露出計の較正 K = 12.5（L = 2^EV100 × K / 100）。
constexpr float kMiddleGray = 0.18f;
constexpr float kMeterCalibration = 12.5f / 100.0f;
// 昼の光の入射 EV100。これより暗い天気を暗く残す。
constexpr float kDaylightEv100 = 15.0f;

float SanitizePreviousPreExposure(float previous, float current) noexcept {
    return std::isfinite(previous) && previous > 0.0f ? previous : current;
}

struct ColorScale {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

ColorScale BlackBodyScale(float kelvin) noexcept {
    const float temperature = std::clamp(kelvin, 2000.0f, 15000.0f) / 100.0f;
    ColorScale scale{};
    if (temperature <= 66.0f) {
        scale.r = 1.0f;
        scale.g = std::clamp((99.4708025861f * std::log(temperature) - 161.1195681661f) / 255.0f, 0.0f, 1.0f);
        scale.b =
            temperature <= 19.0f
                ? 0.0f
                : std::clamp((138.5177312231f * std::log(temperature - 10.0f) - 305.0447927307f) / 255.0f, 0.0f, 1.0f);
    } else {
        scale.r = std::clamp((329.698727446f * std::pow(temperature - 60.0f, -0.1332047592f)) / 255.0f, 0.0f, 1.0f);
        scale.g = std::clamp((288.1221695283f * std::pow(temperature - 60.0f, -0.0755148492f)) / 255.0f, 0.0f, 1.0f);
        scale.b = 1.0f;
    }
    return scale;
}
} // namespace

ExposureFrameState BuildManualExposureFrame(float sceneExposure, float previousPreExposure, float viewExposure) {
    ExposureFrameState state{};
    const bool finite = std::isfinite(sceneExposure) && std::isfinite(viewExposure);
    const float safeSceneExposure = finite ? (std::max)(0.0f, sceneExposure) : 1.0f;
    const float safeViewExposure = finite ? (std::max)(0.0f, viewExposure) : 1.0f;

    state.mode = ExposureMode::Manual;
    state.exposure = safeSceneExposure * safeViewExposure;
    state.meteredLuminance = 0.18f;
    state.meteringValid = false;
    state.valid = finite && std::isfinite(state.exposure) && state.exposure > 0.0f;
    if (!state.valid) {
        state.exposure = 1.0f;
    }
    state.targetExposure = state.exposure;
    state.preExposure = state.exposure;
    state.previousPreExposure = SanitizePreviousPreExposure(previousPreExposure, state.preExposure);
    return state;
}

float ExposureFromEv100(float ev100) noexcept {
    const float safeEv = std::isfinite(ev100) ? ev100 : 0.0f;
    return kMiddleGray / (kMeterCalibration * std::exp2(safeEv));
}

float Ev100FromExposure(float exposure) noexcept {
    return std::log2(kMiddleGray / (kMeterCalibration * SanitizePositive(exposure, 1.0f)));
}

float ResolveManualExposure(const ExposureSettings& settings) noexcept {
    const float compensation = std::isfinite(settings.compensationEv) ? settings.compensationEv : 0.0f;
    return ExposureFromEv100(settings.manualEv100 - compensation);
}

ExposureFrameState BuildAutomaticExposureFrame(const ExposureSettings& settings,
                                               const ExposureIllumination& illumination, float measuredLuminance,
                                               float highlightLuminance, float previousExposure,
                                               float previousPreExposure, float deltaTime, bool measurementValid) {
    ExposureFrameState state{};
    state.mode = ExposureMode::Automatic;

    const float safeMeasuredLuminance = SanitizePositive(measuredLuminance, kMiddleGray);
    const float safePreviousExposure = SanitizePositive(previousExposure, 1.0f);
    // EV100 が大きい（明るい場面）ほど E は小さい。範囲は log2 E で持つ。
    const float minLog2 =
        std::clamp(std::log2(ExposureFromEv100(settings.maxEv100)), kMinimumLog2Exposure, kMaximumLog2Exposure);
    const float maxLog2 = std::clamp(std::log2(ExposureFromEv100(settings.minEv100)), minLog2, kMaximumLog2Exposure);
    const float compensationEv =
        std::isfinite(settings.compensationEv) ? std::clamp(settings.compensationEv, -8.0f, 8.0f) : 0.0f;
    // 反射式露出計の較正（K = 12.5、ISO 100）で平均輝度 L の EV100 は log2(8 L)。
    float measuredEv100 = std::log2(safeMeasuredLuminance * 8.0f);
    const float nightKnee = std::isfinite(settings.nightAdaptationEv100) ? settings.nightAdaptationEv100 : 5.0f;
    float dimEv = 0.0f;
    if (std::isfinite(illumination.incidentEv100)) {
        // 直射日光の下では、入射照度の露出より暗くする分を最大 sunlitMeteringCorrectionEv まで戻す。
        const float sun = std::isfinite(illumination.directSun) ? std::clamp(illumination.directSun, 0.0f, 1.0f) : 0.0f;
        const float correction = std::isfinite(settings.sunlitMeteringCorrectionEv)
                                     ? std::clamp(settings.sunlitMeteringCorrectionEv, 0.0f, 4.0f)
                                     : 0.0f;
        measuredEv100 -= sun * (std::min)((std::max)(0.0f, measuredEv100 - illumination.incidentEv100), correction);
        // 昼より暗い天気は照度の差の一部を残す。夜へ近づく（入射 EV100 が暗所の閾値 + 5 より下）ほど暗所の順応へ渡す。
        const float dimStrength = std::isfinite(settings.dimAdaptationStrength)
                                      ? std::clamp(settings.dimAdaptationStrength, 0.0f, 1.0f)
                                      : 0.0f;
        const float handOver = std::clamp((illumination.incidentEv100 - nightKnee) / 5.0f, 0.0f, 1.0f);
        dimEv = dimStrength * (std::max)(0.0f, kDaylightEv100 - illumination.incidentEv100) * handOver * handOver *
                (3.0f - 2.0f * handOver);
    }
    // 暗い場面は測った EV100 と閾値の差の一部を露出に反映しない（目は暗所で完全には順応しない）。
    const float nightStrength = std::isfinite(settings.nightAdaptationStrength)
                                    ? std::clamp(settings.nightAdaptationStrength, 0.0f, 1.0f)
                                    : 0.0f;
    const float adaptedEv100 = measuredEv100 + nightStrength * (std::max)(0.0f, nightKnee - measuredEv100) + dimEv;
    float targetLog2 = std::log2(ExposureFromEv100(adaptedEv100)) + compensationEv;
    // 明部が中間灰の 3.5 段上を超えるなら、その超過の一部だけ露出を下げる。
    const float highlightLimitLog2 =
        std::log2(kMiddleGray * 11.3137085f / SanitizePositive(highlightLuminance, safeMeasuredLuminance));
    const float protection =
        std::isfinite(settings.highlightProtection) ? std::clamp(settings.highlightProtection, 0.0f, 1.0f) : 0.0f;
    targetLog2 -= protection * (std::max)(0.0f, targetLog2 - highlightLimitLog2);
    // 適応は対数（EV）領域で行い、明暗どちらへも同じ知覚速度で近づける。
    targetLog2 = std::clamp(targetLog2, minLog2, maxLog2);
    const float previousLog2 = std::log2(safePreviousExposure);

    const float safeDeltaTime = std::isfinite(deltaTime) ? std::clamp(deltaTime, 0.0f, 0.25f) : 0.0f;
    const bool adaptingToBrighterScene = targetLog2 < previousLog2;
    const float selectedSpeed = adaptingToBrighterScene ? settings.speedUp : settings.speedDown;
    const float speed = std::clamp(std::isfinite(selectedSpeed) ? selectedSpeed : 1.0f, 0.0f, 20.0f);
    const float blend = measurementValid ? 1.0f - std::exp(-speed * safeDeltaTime) : 0.0f;

    state.exposure = std::exp2(previousLog2 + (targetLog2 - previousLog2) * blend);
    state.meteredLuminance = safeMeasuredLuminance;
    state.targetExposure = std::exp2(targetLog2);
    state.meteringValid = measurementValid;
    state.valid = std::isfinite(state.exposure) && state.exposure > 0.0f;
    if (!state.valid) {
        state.exposure = 1.0f;
        state.targetExposure = 1.0f;
    }
    state.preExposure = state.exposure;
    state.previousPreExposure = SanitizePreviousPreExposure(previousPreExposure, state.preExposure);
    return state;
}

DisplayColorFrameState BuildDisplayColorFrame(float whiteBalanceTemperature, float whiteBalanceTint, float contrast,
                                              float saturation) {
    DisplayColorFrameState state{};
    const bool finite = std::isfinite(whiteBalanceTemperature) && std::isfinite(whiteBalanceTint) &&
                        std::isfinite(contrast) && std::isfinite(saturation);
    const float temperature = finite ? std::clamp(whiteBalanceTemperature, 2000.0f, 15000.0f) : 6500.0f;
    const float tint = finite ? std::clamp(whiteBalanceTint, -1.0f, 1.0f) : 0.0f;
    const ColorScale reference = BlackBodyScale(6500.0f);
    const ColorScale requested = BlackBodyScale(temperature);
    const float magentaScale = std::exp2(tint * 0.25f);
    const float greenScale = std::exp2(-tint * 0.5f);
    state.whiteBalanceR = reference.r / (std::max)(requested.r, 1e-4f) * magentaScale;
    state.whiteBalanceG = reference.g / (std::max)(requested.g, 1e-4f) * greenScale;
    state.whiteBalanceB = reference.b / (std::max)(requested.b, 1e-4f) * magentaScale;
    const float luminanceScale =
        state.whiteBalanceR * 0.2126f + state.whiteBalanceG * 0.7152f + state.whiteBalanceB * 0.0722f;
    state.whiteBalanceR /= (std::max)(luminanceScale, 1e-4f);
    state.whiteBalanceG /= (std::max)(luminanceScale, 1e-4f);
    state.whiteBalanceB /= (std::max)(luminanceScale, 1e-4f);
    state.contrast = finite ? std::clamp(contrast, 0.25f, 2.0f) : 1.0f;
    state.saturation = finite ? std::clamp(saturation, 0.0f, 2.0f) : 1.0f;
    state.valid = finite && std::isfinite(state.whiteBalanceR) && std::isfinite(state.whiteBalanceG) &&
                  std::isfinite(state.whiteBalanceB);
    if (!state.valid) {
        state = {};
    }
    return state;
}

float ResolveTemporalExposure(const ExposureFrameState& state) noexcept {
    return state.valid && state.exposure > 0.0f ? state.exposure : 1.0f;
}

float ResidualExposure(const ExposureFrameState& state) noexcept {
    return state.valid && state.preExposure > 0.0f ? state.exposure / state.preExposure : 1.0f;
}

float PreExposureRatio(const ExposureFrameState& state) noexcept {
    return state.valid && state.preExposure > 0.0f && state.previousPreExposure > 0.0f
               ? state.preExposure / state.previousPreExposure
               : 1.0f;
}

} // namespace HIKARI::RENDER3D::COLOR
