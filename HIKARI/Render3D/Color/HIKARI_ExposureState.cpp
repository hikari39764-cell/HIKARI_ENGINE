#include "Render3D/Color/HIKARI_ExposureState.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::RENDER3D::COLOR {

    namespace {
        float SanitizePositive(float value, float fallback) noexcept {
            return std::isfinite(value) && value > 0.0f
                ? value
                : fallback;
        }

        float ExposureFromEv(float ev) noexcept {
            return std::exp2(std::clamp(ev, -16.0f, 16.0f));
        }

        struct ColorScale {
            float r = 1.0f;
            float g = 1.0f;
            float b = 1.0f;
        };

        ColorScale BlackBodyScale(float kelvin) noexcept {
            const float temperature =
                std::clamp(kelvin, 2000.0f, 15000.0f) / 100.0f;
            ColorScale scale{};
            if (temperature <= 66.0f) {
                scale.r = 1.0f;
                scale.g = std::clamp(
                    (99.4708025861f * std::log(temperature) -
                        161.1195681661f) / 255.0f,
                    0.0f,
                    1.0f);
                scale.b = temperature <= 19.0f
                    ? 0.0f
                    : std::clamp(
                        (138.5177312231f *
                            std::log(temperature - 10.0f) -
                            305.0447927307f) / 255.0f,
                        0.0f,
                        1.0f);
            } else {
                scale.r = std::clamp(
                    (329.698727446f *
                        std::pow(temperature - 60.0f, -0.1332047592f)) /
                        255.0f,
                    0.0f,
                    1.0f);
                scale.g = std::clamp(
                    (288.1221695283f *
                        std::pow(temperature - 60.0f, -0.0755148492f)) /
                        255.0f,
                    0.0f,
                    1.0f);
                scale.b = 1.0f;
            }
            return scale;
        }
    }

    ExposureFrameState BuildManualExposureFrame(
        float sceneExposure,
        float viewExposure) {

        ExposureFrameState state{};
        const bool finite =
            std::isfinite(sceneExposure) &&
            std::isfinite(viewExposure);
        const float safeSceneExposure =
            finite ? (std::max)(0.0f, sceneExposure) : 1.0f;
        const float safeViewExposure =
            finite ? (std::max)(0.0f, viewExposure) : 1.0f;

        state.mode = ExposureMode::Manual;
        state.exposureMultiplier =
            safeSceneExposure * safeViewExposure;
        state.preExposure = 1.0f;
        state.previousPreExposure = 1.0f;
        state.exposureCompensation = safeViewExposure;
        state.meteredLuminance = 0.18f;
        state.targetExposureMultiplier = state.exposureMultiplier;
        state.meteringValid = false;
        state.valid = finite &&
            std::isfinite(state.exposureMultiplier);
        if (!state.valid) {
            state.exposureMultiplier = 1.0f;
            state.exposureCompensation = 1.0f;
            state.targetExposureMultiplier = 1.0f;
        }
        return state;
    }

    ExposureFrameState BuildAutomaticExposureFrame(
        const AutomaticExposureSettings& settings,
        float measuredLuminance,
        float previousExposureMultiplier,
        float deltaTime,
        bool measurementValid) {

        ExposureFrameState state{};
        state.mode = ExposureMode::Automatic;
        state.preExposure = 1.0f;
        state.previousPreExposure = 1.0f;

        const float targetLuminance =
            SanitizePositive(settings.targetLuminance, 0.18f);
        const float safeMeasuredLuminance =
            SanitizePositive(measuredLuminance, targetLuminance);
        const float safePreviousExposure =
            SanitizePositive(previousExposureMultiplier, 1.0f);
        const float minEv = std::clamp(
            std::isfinite(settings.minExposureEv)
                ? settings.minExposureEv
                : -4.0f,
            -16.0f,
            16.0f);
        const float maxEv = std::clamp(
            std::isfinite(settings.maxExposureEv)
                ? settings.maxExposureEv
                : 4.0f,
            minEv,
            16.0f);
        const float compensationEv =
            std::isfinite(settings.compensationEv)
                ? std::clamp(settings.compensationEv, -8.0f, 8.0f)
                : 0.0f;
        const float unclampedTarget =
            targetLuminance / safeMeasuredLuminance *
            ExposureFromEv(compensationEv);
        const float targetExposure = std::clamp(
            unclampedTarget,
            ExposureFromEv(minEv),
            ExposureFromEv(maxEv));

        const float safeDeltaTime = std::isfinite(deltaTime)
            ? std::clamp(deltaTime, 0.0f, 0.25f)
            : 0.0f;
        const bool adaptingToBrighterScene =
            targetExposure < safePreviousExposure;
        const float selectedSpeed = adaptingToBrighterScene
            ? settings.speedUp
            : settings.speedDown;
        const float speed = std::clamp(
            std::isfinite(selectedSpeed) ? selectedSpeed : 1.0f,
            0.0f,
            20.0f);
        const float blend = measurementValid
            ? 1.0f - std::exp(-speed * safeDeltaTime)
            : 0.0f;

        state.exposureMultiplier =
            safePreviousExposure +
            (targetExposure - safePreviousExposure) * blend;
        state.exposureCompensation = ExposureFromEv(compensationEv);
        state.meteredLuminance = safeMeasuredLuminance;
        state.targetExposureMultiplier = targetExposure;
        state.meteringValid = measurementValid;
        state.valid =
            std::isfinite(state.exposureMultiplier) &&
            state.exposureMultiplier > 0.0f;
        if (!state.valid) {
            state.exposureMultiplier = 1.0f;
            state.targetExposureMultiplier = 1.0f;
        }
        return state;
    }

    DisplayColorFrameState BuildDisplayColorFrame(
        float whiteBalanceTemperature,
        float whiteBalanceTint,
        float contrast,
        float saturation) {

        DisplayColorFrameState state{};
        const bool finite =
            std::isfinite(whiteBalanceTemperature) &&
            std::isfinite(whiteBalanceTint) &&
            std::isfinite(contrast) &&
            std::isfinite(saturation);
        const float temperature = finite
            ? std::clamp(whiteBalanceTemperature, 2000.0f, 15000.0f)
            : 6500.0f;
        const float tint = finite
            ? std::clamp(whiteBalanceTint, -1.0f, 1.0f)
            : 0.0f;
        const ColorScale reference = BlackBodyScale(6500.0f);
        const ColorScale requested = BlackBodyScale(temperature);
        const float magentaScale = std::exp2(tint * 0.25f);
        const float greenScale = std::exp2(-tint * 0.5f);
        state.whiteBalanceR =
            reference.r / (std::max)(requested.r, 1e-4f) *
            magentaScale;
        state.whiteBalanceG =
            reference.g / (std::max)(requested.g, 1e-4f) *
            greenScale;
        state.whiteBalanceB =
            reference.b / (std::max)(requested.b, 1e-4f) *
            magentaScale;
        const float luminanceScale =
            state.whiteBalanceR * 0.2126f +
            state.whiteBalanceG * 0.7152f +
            state.whiteBalanceB * 0.0722f;
        state.whiteBalanceR /= (std::max)(luminanceScale, 1e-4f);
        state.whiteBalanceG /= (std::max)(luminanceScale, 1e-4f);
        state.whiteBalanceB /= (std::max)(luminanceScale, 1e-4f);
        state.contrast = finite
            ? std::clamp(contrast, 0.25f, 2.0f)
            : 1.0f;
        state.saturation = finite
            ? std::clamp(saturation, 0.0f, 2.0f)
            : 1.0f;
        state.valid = finite &&
            std::isfinite(state.whiteBalanceR) &&
            std::isfinite(state.whiteBalanceG) &&
            std::isfinite(state.whiteBalanceB);
        if (!state.valid) {
            state = {};
        }
        return state;
    }

    float ResolveTemporalExposure(
        const ExposureFrameState& state) noexcept {

        return state.valid
            ? (std::max)(state.exposureMultiplier, 1e-4f)
            : 1.0f;
    }

    const char* ToString(ExposureMode mode) noexcept {
        switch (mode) {
        case ExposureMode::Manual:
            return "manual";
        case ExposureMode::Automatic:
            return "automatic";
        default:
            return "unknown";
        }
    }

} // namespace HIKARI::RENDER3D::COLOR
