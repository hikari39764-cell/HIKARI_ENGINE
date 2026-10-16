#include "Scene/Camera/HIKARI_CameraFollowSettings.h"
#include "Core/HIKARI_JsonRead.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include <algorithm>
#include <cmath>

namespace HIKARI::CAMERA {
namespace JsonMath = SERIALIZATION::JSON::MATH;
namespace {
constexpr float kMinimumRange = 0.01f;

float NonNegativeFinite(float value, float fallback) noexcept {
    return (std::max)(MATH::FiniteOr(value, fallback), 0.0f);
}

} // namespace
void CameraFollowSettings::Serialize(nlohmann::json& out) const {
    out["pivotOffset"] = JsonMath::ToJsonArray(pivotOffset);
    out["initialYawDegrees"] = initialYawDegrees;
    out["initialPitchDegrees"] = initialPitchDegrees;
    out["initialDistance"] = initialDistance;
    out["minimumPitchDegrees"] = minimumPitchDegrees;
    out["maximumPitchDegrees"] = maximumPitchDegrees;
    out["minimumDistance"] = minimumDistance;
    out["maximumDistance"] = maximumDistance;
    out["followSmooth"] = followSmooth;
    out["lookSmooth"] = lookSmooth;
    out["distanceSmooth"] = distanceSmooth;
    out["lookAheadDistance"] = lookAheadDistance;
    out["verticalDeadZone"] = verticalDeadZone;
    out["orbitInputEnabled"] = orbitInputEnabled;
    out["lookAction"] = lookActionId;
    out["zoomAction"] = zoomActionId;
    out["recenterAction"] = recenterActionId;
    out["yawSpeedDegreesPerSecond"] = yawSpeedDegreesPerSecond;
    out["pitchSpeedDegreesPerSecond"] = pitchSpeedDegreesPerSecond;
    out["mouseSensitivityDegreesPerPixel"] = mouseSensitivityDegreesPerPixel;
    out["zoomSpeedUnitsPerSecond"] = zoomSpeedUnitsPerSecond;
    out["mouseWheelZoomUnitsPerStep"] = mouseWheelZoomUnitsPerStep;
    out["invertVerticalLook"] = invertVerticalLook;
    out["autoRecenterEnabled"] = autoRecenterEnabled;
    out["autoRecenterDelaySeconds"] = autoRecenterDelaySeconds;
    out["autoRecenterSpeedDegreesPerSecond"] = autoRecenterSpeedDegreesPerSecond;
    out["collisionEnabled"] = collisionEnabled;
    out["collisionStaticOnly"] = collisionStaticOnly;
    out["collisionRadius"] = collisionRadius;
    out["collisionPadding"] = collisionPadding;
    out["collisionLayerMask"] = collisionLayerMask;
    out["screenCompositionEnabled"] = screenCompositionEnabled;
    out["screenCompositionPrimaryAnchorX"] = screenCompositionPrimaryAnchorX;
    out["screenCompositionPrimaryAnchorY"] = screenCompositionPrimaryAnchorY;
    out["screenCompositionSecondaryAnchorX"] = screenCompositionSecondaryAnchorX;
    out["screenCompositionSecondaryAnchorY"] = screenCompositionSecondaryAnchorY;
    out["screenCompositionSoftZoneHalfWidth"] = screenCompositionSoftZoneHalfWidth;
    out["screenCompositionSoftZoneHalfHeight"] = screenCompositionSoftZoneHalfHeight;
    out["screenCompositionHardZoneHalfWidth"] = screenCompositionHardZoneHalfWidth;
    out["screenCompositionHardZoneHalfHeight"] = screenCompositionHardZoneHalfHeight;
    out["screenCompositionPreferredShoulderAngleDegrees"] = screenCompositionPreferredShoulderAngleDegrees;
    out["screenCompositionMaximumShoulderAngleDegrees"] = screenCompositionMaximumShoulderAngleDegrees;
    out["screenCompositionFocusHeightBias"] = screenCompositionFocusHeightBias;
    out["screenCompositionHardResponseScale"] = screenCompositionHardResponseScale;
    out["screenCompositionCollisionCenteringStrength"] = screenCompositionCollisionCenteringStrength;
}
void CameraFollowSettings::Deserialize(const nlohmann::json& in) {
    if (in.contains("pivotOffset")) {
        pivotOffset = JSONREAD::Vec3Or(in["pivotOffset"], pivotOffset);
    }
    initialYawDegrees = in.value("initialYawDegrees", initialYawDegrees);
    initialPitchDegrees = in.value("initialPitchDegrees", initialPitchDegrees);
    initialDistance = in.value("initialDistance", initialDistance);

    minimumPitchDegrees = in.value("minimumPitchDegrees", minimumPitchDegrees);
    maximumPitchDegrees = in.value("maximumPitchDegrees", maximumPitchDegrees);
    minimumDistance = in.value("minimumDistance", minimumDistance);
    maximumDistance = in.value("maximumDistance", maximumDistance);
    followSmooth = in.value("followSmooth", followSmooth);
    lookSmooth = in.value("lookSmooth", lookSmooth);
    distanceSmooth = in.value("distanceSmooth", distanceSmooth);
    lookAheadDistance = in.value("lookAheadDistance", lookAheadDistance);
    verticalDeadZone = in.value("verticalDeadZone", verticalDeadZone);
    orbitInputEnabled = in.value("orbitInputEnabled", orbitInputEnabled);
    lookActionId = in.value("lookAction", lookActionId);
    zoomActionId = in.value("zoomAction", zoomActionId);
    recenterActionId = in.value("recenterAction", recenterActionId);
    yawSpeedDegreesPerSecond = in.value("yawSpeedDegreesPerSecond", yawSpeedDegreesPerSecond);
    pitchSpeedDegreesPerSecond = in.value("pitchSpeedDegreesPerSecond", pitchSpeedDegreesPerSecond);
    mouseSensitivityDegreesPerPixel = in.value("mouseSensitivityDegreesPerPixel", mouseSensitivityDegreesPerPixel);
    zoomSpeedUnitsPerSecond = in.value("zoomSpeedUnitsPerSecond", zoomSpeedUnitsPerSecond);
    mouseWheelZoomUnitsPerStep = in.value("mouseWheelZoomUnitsPerStep", mouseWheelZoomUnitsPerStep);
    invertVerticalLook = in.value("invertVerticalLook", invertVerticalLook);
    autoRecenterEnabled = in.value("autoRecenterEnabled", autoRecenterEnabled);
    autoRecenterDelaySeconds = in.value("autoRecenterDelaySeconds", autoRecenterDelaySeconds);
    autoRecenterSpeedDegreesPerSecond =
        in.value("autoRecenterSpeedDegreesPerSecond", autoRecenterSpeedDegreesPerSecond);
    collisionEnabled = in.value("collisionEnabled", collisionEnabled);
    collisionStaticOnly = in.value("collisionStaticOnly", collisionStaticOnly);
    collisionRadius = in.value("collisionRadius", collisionRadius);
    collisionPadding = in.value("collisionPadding", collisionPadding);
    collisionLayerMask = in.value("collisionLayerMask", collisionLayerMask);
    screenCompositionEnabled = in.value("screenCompositionEnabled", screenCompositionEnabled);
    screenCompositionPrimaryAnchorX = in.value("screenCompositionPrimaryAnchorX", screenCompositionPrimaryAnchorX);
    screenCompositionPrimaryAnchorY = in.value("screenCompositionPrimaryAnchorY", screenCompositionPrimaryAnchorY);
    screenCompositionSecondaryAnchorX =
        in.value("screenCompositionSecondaryAnchorX", screenCompositionSecondaryAnchorX);
    screenCompositionSecondaryAnchorY =
        in.value("screenCompositionSecondaryAnchorY", screenCompositionSecondaryAnchorY);
    screenCompositionSoftZoneHalfWidth =
        in.value("screenCompositionSoftZoneHalfWidth", screenCompositionSoftZoneHalfWidth);
    screenCompositionSoftZoneHalfHeight =
        in.value("screenCompositionSoftZoneHalfHeight", screenCompositionSoftZoneHalfHeight);
    screenCompositionHardZoneHalfWidth =
        in.value("screenCompositionHardZoneHalfWidth", screenCompositionHardZoneHalfWidth);
    screenCompositionHardZoneHalfHeight =
        in.value("screenCompositionHardZoneHalfHeight", screenCompositionHardZoneHalfHeight);
    screenCompositionPreferredShoulderAngleDegrees =
        in.value("screenCompositionPreferredShoulderAngleDegrees", screenCompositionPreferredShoulderAngleDegrees);
    screenCompositionMaximumShoulderAngleDegrees =
        in.value("screenCompositionMaximumShoulderAngleDegrees", screenCompositionMaximumShoulderAngleDegrees);
    screenCompositionFocusHeightBias = in.value("screenCompositionFocusHeightBias", screenCompositionFocusHeightBias);
    screenCompositionHardResponseScale =
        in.value("screenCompositionHardResponseScale", screenCompositionHardResponseScale);
    screenCompositionCollisionCenteringStrength =
        in.value("screenCompositionCollisionCenteringStrength", screenCompositionCollisionCenteringStrength);

    Normalize();
}
void CameraFollowSettings::Normalize() noexcept {
    pivotOffset = MATH::FiniteOr(pivotOffset, MATH::Vec3{0.0f, 1.2f, 0.0f});
    initialYawDegrees = MATH::FiniteOr(initialYawDegrees, 0.0f);
    minimumPitchDegrees = MATH::FiniteOr(minimumPitchDegrees, -65.0f);
    maximumPitchDegrees = MATH::FiniteOr(maximumPitchDegrees, 75.0f);
    initialPitchDegrees = MATH::FiniteOr(initialPitchDegrees, 28.0f);
    minimumDistance = MATH::FiniteOr(minimumDistance, 1.0f);
    maximumDistance = MATH::FiniteOr(maximumDistance, 20.0f);
    initialDistance = MATH::FiniteOr(initialDistance, 8.0f);
    minimumPitchDegrees = std::clamp(minimumPitchDegrees, -89.0f, 89.0f);
    maximumPitchDegrees = std::clamp(maximumPitchDegrees, minimumPitchDegrees, 89.0f);
    initialPitchDegrees = std::clamp(initialPitchDegrees, minimumPitchDegrees, maximumPitchDegrees);
    minimumDistance = (std::max)(minimumDistance, kMinimumRange);
    maximumDistance = (std::max)(maximumDistance, minimumDistance);
    initialDistance = std::clamp(initialDistance, minimumDistance, maximumDistance);
    followSmooth = NonNegativeFinite(followSmooth, 12.0f);
    lookSmooth = NonNegativeFinite(lookSmooth, 14.0f);
    distanceSmooth = NonNegativeFinite(distanceSmooth, 18.0f);
    lookAheadDistance = (std::min)(NonNegativeFinite(lookAheadDistance, 0.0f), 20.0f);
    verticalDeadZone = (std::min)(NonNegativeFinite(verticalDeadZone, 0.0f), 10.0f);
    yawSpeedDegreesPerSecond = NonNegativeFinite(yawSpeedDegreesPerSecond, 180.0f);
    pitchSpeedDegreesPerSecond = NonNegativeFinite(pitchSpeedDegreesPerSecond, 140.0f);
    mouseSensitivityDegreesPerPixel = NonNegativeFinite(mouseSensitivityDegreesPerPixel, 0.12f);
    zoomSpeedUnitsPerSecond = NonNegativeFinite(zoomSpeedUnitsPerSecond, 30.0f);
    mouseWheelZoomUnitsPerStep = NonNegativeFinite(mouseWheelZoomUnitsPerStep, 1.0f);
    autoRecenterDelaySeconds = NonNegativeFinite(autoRecenterDelaySeconds, 1.25f);
    autoRecenterSpeedDegreesPerSecond = NonNegativeFinite(autoRecenterSpeedDegreesPerSecond, 90.0f);
    collisionRadius = (std::max)(NonNegativeFinite(collisionRadius, 0.25f), 0.01f);
    collisionPadding = NonNegativeFinite(collisionPadding, 0.08f);
    screenCompositionPrimaryAnchorX = std::clamp(MATH::FiniteOr(screenCompositionPrimaryAnchorX, 0.38f), 0.05f, 0.95f);
    screenCompositionPrimaryAnchorY = std::clamp(MATH::FiniteOr(screenCompositionPrimaryAnchorY, 0.58f), 0.05f, 0.95f);
    screenCompositionSecondaryAnchorX =
        std::clamp(MATH::FiniteOr(screenCompositionSecondaryAnchorX, 0.62f), 0.05f, 0.95f);
    if (screenCompositionPrimaryAnchorX > screenCompositionSecondaryAnchorX) {
        std::swap(screenCompositionPrimaryAnchorX, screenCompositionSecondaryAnchorX);
    }
    if (screenCompositionSecondaryAnchorX - screenCompositionPrimaryAnchorX < 0.04f) {
        const float center =
            std::clamp((screenCompositionPrimaryAnchorX + screenCompositionSecondaryAnchorX) * 0.5f, 0.07f, 0.93f);
        screenCompositionPrimaryAnchorX = center - 0.02f;
        screenCompositionSecondaryAnchorX = center + 0.02f;
    }
    screenCompositionSecondaryAnchorY =
        std::clamp(MATH::FiniteOr(screenCompositionSecondaryAnchorY, 0.46f), 0.05f, 0.95f);
    screenCompositionSoftZoneHalfWidth =
        std::clamp(MATH::FiniteOr(screenCompositionSoftZoneHalfWidth, 0.075f), 0.0f, 0.45f);
    screenCompositionSoftZoneHalfHeight =
        std::clamp(MATH::FiniteOr(screenCompositionSoftZoneHalfHeight, 0.10f), 0.0f, 0.45f);
    screenCompositionHardZoneHalfWidth = std::clamp(MATH::FiniteOr(screenCompositionHardZoneHalfWidth, 0.20f),
                                                    screenCompositionSoftZoneHalfWidth + 0.01f, 0.50f);
    screenCompositionHardZoneHalfHeight = std::clamp(MATH::FiniteOr(screenCompositionHardZoneHalfHeight, 0.24f),
                                                     screenCompositionSoftZoneHalfHeight + 0.01f, 0.50f);
    screenCompositionPreferredShoulderAngleDegrees =
        std::clamp(MATH::FiniteOr(screenCompositionPreferredShoulderAngleDegrees, 32.0f), 0.0f, 89.0f);
    screenCompositionMaximumShoulderAngleDegrees =
        std::clamp(MATH::FiniteOr(screenCompositionMaximumShoulderAngleDegrees, 58.0f),
                   screenCompositionPreferredShoulderAngleDegrees, 89.0f);
    screenCompositionFocusHeightBias = std::clamp(MATH::FiniteOr(screenCompositionFocusHeightBias, 0.22f), -5.0f, 5.0f);
    screenCompositionHardResponseScale =
        std::clamp(MATH::FiniteOr(screenCompositionHardResponseScale, 0.45f), 0.10f, 1.0f);
    screenCompositionCollisionCenteringStrength =
        std::clamp(MATH::FiniteOr(screenCompositionCollisionCenteringStrength, 0.72f), 0.0f, 1.0f);
}
} // namespace HIKARI::CAMERA
