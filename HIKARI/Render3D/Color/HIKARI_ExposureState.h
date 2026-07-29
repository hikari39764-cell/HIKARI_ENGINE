#pragma once

#include <cstdint>

namespace HIKARI::RENDER3D::COLOR {

    enum class ExposureMode : uint8_t {
        Manual = 0u,
        Automatic = 1u,
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
        bool valid = true;
    };

    ExposureFrameState BuildManualExposureFrame(
        float sceneExposure,
        float viewExposure = 1.0f);
    float ResolveTemporalExposure(
        const ExposureFrameState& state) noexcept;
    const char* ToString(ExposureMode mode) noexcept;

} // namespace HIKARI::RENDER3D::COLOR
