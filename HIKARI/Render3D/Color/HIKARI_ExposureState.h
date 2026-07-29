#pragma once

#include <cstdint>

namespace HIKARI::RENDER3D::COLOR {

    enum class ExposureMode : uint8_t {
        Manual = 0u,
        Automatic = 1u,
    };

    struct AutomaticExposureSettings {
        float targetLuminance = 0.18f;
        float compensationEv = 0.0f;
        float minExposureEv = -4.0f;
        float maxExposureEv = 4.0f;
        float speedUp = 3.0f;
        float speedDown = 1.0f;
    };

    // Scene-linear HDR から表示変換までのフレーム露出契約。
    // 現在の SceneColor は未 Pre-Exposure のため preExposure は 1。
    // Histogram / Eye Adaptation 導入時も Temporal、Upscaler、Post は
    // この契約だけを参照する。
    struct ExposureFrameState {
        ExposureMode mode = ExposureMode::Manual;
        float exposureMultiplier = 1.0f;
        float preExposure = 1.0f;
        float previousPreExposure = 1.0f;
        float exposureCompensation = 1.0f;
        float meteredLuminance = 0.18f;
        float targetExposureMultiplier = 1.0f;
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

    ExposureFrameState BuildManualExposureFrame(
        float sceneExposure,
        float viewExposure = 1.0f);
    ExposureFrameState BuildAutomaticExposureFrame(
        const AutomaticExposureSettings& settings,
        float measuredLuminance,
        float previousExposureMultiplier,
        float deltaTime,
        bool measurementValid);
    DisplayColorFrameState BuildDisplayColorFrame(
        float whiteBalanceTemperature,
        float whiteBalanceTint,
        float contrast,
        float saturation);
    float ResolveTemporalExposure(
        const ExposureFrameState& state) noexcept;
    const char* ToString(ExposureMode mode) noexcept;

} // namespace HIKARI::RENDER3D::COLOR
