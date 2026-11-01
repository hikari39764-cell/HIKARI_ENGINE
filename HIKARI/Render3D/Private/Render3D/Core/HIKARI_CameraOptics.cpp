#include "Render3D/Core/HIKARI_CameraOptics.h"

#include "Render3D/Core/HIKARI_CameraOpticsFields.h"

namespace HIKARI::CAMERA {

DepthOfFieldSettings NormalizeDepthOfFieldSettings(DepthOfFieldSettings settings) noexcept {
    return JSONFIELDS::Sanitize(settings, kDepthOfFieldFields);
}

bool IsValidDepthOfFieldSettings(const DepthOfFieldSettings& settings) noexcept {
    return NormalizeDepthOfFieldSettings(settings) == settings;
}

MotionBlurSettings NormalizeMotionBlurSettings(MotionBlurSettings settings) noexcept {
    return JSONFIELDS::Sanitize(settings, kMotionBlurFields);
}

} // namespace HIKARI::CAMERA
