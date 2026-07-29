#include "Render3D/Color/HIKARI_ExposureState.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::RENDER3D::COLOR {

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
        state.valid = finite &&
            std::isfinite(state.exposureMultiplier);
        if (!state.valid) {
            state.exposureMultiplier = 1.0f;
            state.exposureCompensation = 1.0f;
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
