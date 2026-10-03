#pragma once

#include <cmath>
#include <cstdint>

#include "HIKARI/Project/CameraLensTypes.h"
#include "Render3D/Core/HIKARI_CameraOptics.h"

namespace HIKARI::PROJECT::INTERNAL {

inline GameProjectDepthOfFieldSettings ToProjectDepthOfFieldSettings(
    const CAMERA::DepthOfFieldSettings& settings) noexcept {
    GameProjectDepthOfFieldSettings result{};
    result.enabled = settings.enabled ? 1u : 0u;
    result.focusDistance = settings.focusDistance;
    result.focusRange = settings.focusRange;
    result.nearTransition = settings.nearTransition;
    result.farTransition = settings.farTransition;
    result.nearBlurRadius1080pPixels = settings.nearBlurRadius1080pPixels;
    result.farBlurRadius1080pPixels = settings.farBlurRadius1080pPixels;
    return result;
}

inline bool CopyCameraLens(GameProjectCameraLens& destination, uint64_t sourceCameraObjectHandle,
                           GameProjectCameraLensStage stage, GameProjectCameraProjectionKind projection,
                           float verticalFovRadians, float orthographicHeight, float nearClip, float farClip,
                           const CAMERA::DepthOfFieldSettings& depthOfField) noexcept {
    if (!std::isfinite(verticalFovRadians) || !std::isfinite(orthographicHeight) || !std::isfinite(nearClip) ||
        !std::isfinite(farClip) || nearClip < 0.0f || farClip <= nearClip ||
        !CAMERA::IsValidDepthOfFieldSettings(depthOfField)) {
        return false;
    }
    switch (projection) {
    case GameProjectCameraProjectionKind::Perspective:
        if (verticalFovRadians <= 0.0f || verticalFovRadians >= 3.14159265f) {
            return false;
        }
        break;
    case GameProjectCameraProjectionKind::Orthographic:
        if (orthographicHeight <= 0.0f) {
            return false;
        }
        break;
    default: return false;
    }
    switch (stage) {
    case GameProjectCameraLensStage::Authored:
    case GameProjectCameraLensStage::Control:
    case GameProjectCameraLensStage::Rendered: break;
    default: return false;
    }
    destination.sourceCameraObjectHandle = sourceCameraObjectHandle;
    destination.stage = stage;
    destination.projection = projection;
    destination.verticalFovRadians = verticalFovRadians;
    destination.orthographicHeight = orthographicHeight;
    destination.nearClip = nearClip;
    destination.farClip = farClip;
    destination.depthOfField = ToProjectDepthOfFieldSettings(depthOfField);
    destination.reserved = 0u;
    return true;
}

} // namespace HIKARI::PROJECT::INTERNAL
