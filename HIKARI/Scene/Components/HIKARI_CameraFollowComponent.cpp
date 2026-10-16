#include "HIKARI_CameraFollowComponent.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"
namespace HIKARI {

namespace JsonMath = SERIALIZATION::JSON::MATH;

namespace {

float NonNegativeFinite(float value, float fallback) noexcept {
    return (std::max)(MATH::FiniteOr(value, fallback), 0.0f);
}

CAMERA::CameraTargetFramingDynamics SanitizeDynamics(const CAMERA::CameraTargetFramingDynamics& dynamics) noexcept {
    const CAMERA::CameraTargetFramingDynamics defaults{};
    CAMERA::CameraTargetFramingDynamics result = dynamics;
    result.enterSmoothSeconds = NonNegativeFinite(result.enterSmoothSeconds, defaults.enterSmoothSeconds);
    result.exitSmoothSeconds = NonNegativeFinite(result.exitSmoothSeconds, defaults.exitSmoothSeconds);
    result.targetSwitchSmoothSeconds =
        NonNegativeFinite(result.targetSwitchSmoothSeconds, defaults.targetSwitchSmoothSeconds);
    result.yawSmoothSeconds = NonNegativeFinite(result.yawSmoothSeconds, defaults.yawSmoothSeconds);
    result.pitchSmoothSeconds = NonNegativeFinite(result.pitchSmoothSeconds, defaults.pitchSmoothSeconds);
    result.focusSmoothSeconds = NonNegativeFinite(result.focusSmoothSeconds, defaults.focusSmoothSeconds);
    result.distanceExpandSmoothSeconds =
        NonNegativeFinite(result.distanceExpandSmoothSeconds, defaults.distanceExpandSmoothSeconds);
    result.distanceContractSmoothSeconds =
        NonNegativeFinite(result.distanceContractSmoothSeconds, defaults.distanceContractSmoothSeconds);
    result.yawDeadZoneDegrees =
        std::clamp(MATH::FiniteOr(result.yawDeadZoneDegrees, defaults.yawDeadZoneDegrees), 0.0f, 180.0f);
    return result;
}

} // namespace

void CameraFollowComponent::Serialize(nlohmann::json& out) const {
    out["enabled"] = enabled_;
    out["priority"] = priority_;
    out["targetObjectId"] = SCENE::SERIALIZATION::WriteSceneObjectReference(targetObjectId_.value);
    out["useOwnerAsFallbackTarget"] = useOwnerAsFallbackTarget_;

    settings_.Serialize(out);
    if (gameplayBinding_.profile.IsValid()) {
        out["cameraProfile"] = gameplayBinding_.profile.value;
        out["cameraMode"] = gameplayBinding_.initialMode;
        out["cameraLookTarget"] = SCENE::SERIALIZATION::WriteSceneObjectReference(gameplayBinding_.lookTarget.value);
        out["cameraOverrides"] = gameplayBinding_.overrides;
    }
}

void CameraFollowComponent::Deserialize(const nlohmann::json& in) {
    enabled_ = in.value("enabled", enabled_);
    priority_ = in.value("priority", priority_);
    targetObjectId_.value = SCENE::SERIALIZATION::SceneObjectReferenceOr(in.value("targetObjectId", nlohmann::json{}),
                                                                         targetObjectId_.value);
    useOwnerAsFallbackTarget_ = in.value("useOwnerAsFallbackTarget", useOwnerAsFallbackTarget_);
    evaluatedSettings_.reset();
    settings_.Deserialize(in);
    gameplayBinding_.profile.value = in.value("cameraProfile", std::string{});
    gameplayBinding_.initialMode = in.value("cameraMode", std::string{});
    gameplayBinding_.lookTarget.value =
        SCENE::SERIALIZATION::SceneObjectReferenceOr(in.value("cameraLookTarget", nlohmann::json{}), 0);
    gameplayBinding_.overrides = in.value("cameraOverrides", nlohmann::json::object());
    gameplayState_.Reset();
    ClampSettings();
    ResetRuntimeState();
}

float CameraFollowComponent::GetInitialPitchDegrees() const noexcept {
    return std::clamp(Settings().initialPitchDegrees, Settings().minimumPitchDegrees, Settings().maximumPitchDegrees);
}

float CameraFollowComponent::GetInitialDistance() const noexcept {
    return std::clamp(Settings().initialDistance, Settings().minimumDistance, Settings().maximumDistance);
}

void CameraFollowComponent::InitializeRuntimeState(const MATH::Vec3& eye, const MATH::Vec3& lookAt) noexcept {
    runtime_ = {};
    runtime_.yawDegrees = Settings().initialYawDegrees;
    runtime_.pitchDegrees = GetInitialPitchDegrees();
    runtime_.distance = GetInitialDistance();
    runtime_.desiredDistance = runtime_.distance;
    runtime_.eye = eye;
    runtime_.lookAt = lookAt;
    runtime_.followPivot = lookAt;
    runtime_.targetPivot = lookAt;
    runtime_.initialized = true;
}

void CameraFollowComponent::SetRuntimeTracking(const MATH::Vec3& followPivot, const MATH::Vec3& targetPivot) noexcept {
    runtime_.followPivot = MATH::FiniteOr(followPivot, runtime_.followPivot);
    runtime_.targetPivot = MATH::FiniteOr(targetPivot, runtime_.targetPivot);
}

void CameraFollowComponent::SetRuntimeFramingState(
    bool hasSubmission, RuntimeObjectHandle targetObject, CAMERA::CameraRigSourceId sourceId, int priority,
    const CAMERA::CameraTargetFramingDynamics& dynamics, float targetWeight, float distanceScale,
    float maximumDistanceOffset, float yawTrackingSpeed, float preferredPitchDegrees, float pitchTrackingSpeed,
    bool suppressOrbitInput, bool suppressZoomInput, bool switchActive, float switchRemainingSeconds,
    const MATH::Vec3& secondaryPivot, float framingWeight, float framingWeightVelocity,
    const MATH::Vec3& secondaryPivotVelocity, float yawVelocity, float pitchVelocity, const MATH::Vec3& focusOffset,
    const MATH::Vec3& focusVelocity, float distanceVelocity) noexcept {
    runtime_.framingInitialized = true;
    runtime_.framingHasSubmission = hasSubmission;
    runtime_.framingTargetObject = targetObject;
    runtime_.framingSourceId = sourceId;
    runtime_.framingPriority = priority;
    runtime_.framingDynamics = SanitizeDynamics(dynamics);
    runtime_.framingTargetWeight = std::clamp(MATH::FiniteOr(targetWeight, 0.0f), 0.0f, 1.0f);
    runtime_.framingDistanceScale = NonNegativeFinite(distanceScale, 0.0f);
    runtime_.framingMaximumDistanceOffset = NonNegativeFinite(maximumDistanceOffset, 0.0f);
    runtime_.framingYawTrackingSpeed = NonNegativeFinite(yawTrackingSpeed, 0.0f);
    runtime_.framingPreferredPitchDegrees = MATH::FiniteOr(preferredPitchDegrees, 17.0f);
    runtime_.framingPitchTrackingSpeed = NonNegativeFinite(pitchTrackingSpeed, 0.0f);
    runtime_.framingSuppressOrbitInput = suppressOrbitInput;
    runtime_.framingSuppressZoomInput = suppressZoomInput;
    runtime_.framingSwitchActive = switchActive;
    runtime_.framingSwitchRemainingSeconds = NonNegativeFinite(switchRemainingSeconds, 0.0f);
    runtime_.framingSecondaryPivot = MATH::FiniteOr(secondaryPivot, MATH::Vec3{});
    runtime_.framingWeight = std::clamp(MATH::FiniteOr(framingWeight, 0.0f), 0.0f, 1.0f);
    runtime_.framingWeightVelocity = MATH::FiniteOr(framingWeightVelocity, 0.0f);
    runtime_.framingSecondaryPivotVelocity = MATH::FiniteOr(secondaryPivotVelocity, MATH::Vec3{});
    runtime_.framingYawVelocity = MATH::FiniteOr(yawVelocity, 0.0f);
    runtime_.framingPitchVelocity = MATH::FiniteOr(pitchVelocity, 0.0f);
    runtime_.framingFocusOffset = MATH::FiniteOr(focusOffset, MATH::Vec3{});
    runtime_.framingFocusVelocity = MATH::FiniteOr(focusVelocity, MATH::Vec3{});
    runtime_.framingDistanceVelocity = MATH::FiniteOr(distanceVelocity, 0.0f);
}

void CameraFollowComponent::SetRuntimeScreenCompositionState(bool active, float side, float primaryX, float primaryY,
                                                             float secondaryX, float secondaryY, bool softViolation,
                                                             bool hardViolation, float collisionBlend,
                                                             float collisionBlendVelocity) noexcept {
    runtime_.screenCompositionActive = active;
    runtime_.screenCompositionSide = side >= 0.0f ? 1.0f : -1.0f;
    runtime_.screenCompositionPrimaryX = MATH::FiniteOr(primaryX, 0.5f);
    runtime_.screenCompositionPrimaryY = MATH::FiniteOr(primaryY, 0.5f);
    runtime_.screenCompositionSecondaryX = MATH::FiniteOr(secondaryX, 0.5f);
    runtime_.screenCompositionSecondaryY = MATH::FiniteOr(secondaryY, 0.5f);
    runtime_.screenCompositionSoftViolation = softViolation;
    runtime_.screenCompositionHardViolation = hardViolation;
    runtime_.screenCompositionCollisionBlend = std::clamp(MATH::FiniteOr(collisionBlend, 0.0f), 0.0f, 1.0f);
    runtime_.screenCompositionCollisionBlendVelocity = MATH::FiniteOr(collisionBlendVelocity, 0.0f);
}

void CameraFollowComponent::SetRuntimeRelationshipShotState(bool hasSubmission, float preferredSide,
                                                            float shoulderAngleDegrees, float verticalFovOffsetRadians,
                                                            float rollRadians, float sideClearanceBias,
                                                            float distanceOffset, float pivotHeightOffset,
                                                            float focusLateralOffset, float focusHeightOffset,
                                                            bool suppressModifiers, float yawOffsetDegrees,
                                                            float weight, float weightVelocity) noexcept {
    runtime_.relationshipShotInitialized = true;
    runtime_.relationshipShotHasSubmission = hasSubmission;
    runtime_.relationshipPreferredSide = std::clamp(MATH::FiniteOr(preferredSide, 0.0f), -1.0f, 1.0f);
    runtime_.relationshipShoulderAngleDegrees = std::clamp(NonNegativeFinite(shoulderAngleDegrees, 0.0f), 0.0f, 75.0f);
    runtime_.relationshipVerticalFovOffsetRadians =
        std::clamp(MATH::FiniteOr(verticalFovOffsetRadians, 0.0f), -1.5f, 1.5f);
    runtime_.relationshipRollRadians = std::clamp(MATH::FiniteOr(rollRadians, 0.0f), -3.14159265f, 3.14159265f);
    runtime_.relationshipSideClearanceBias = std::clamp(NonNegativeFinite(sideClearanceBias, 0.0f), 0.0f, 5.0f);
    runtime_.relationshipDistanceOffset = std::clamp(MATH::FiniteOr(distanceOffset, 0.0f), -10.0f, 10.0f);
    runtime_.relationshipPivotHeightOffset = std::clamp(MATH::FiniteOr(pivotHeightOffset, 0.0f), -5.0f, 5.0f);
    runtime_.relationshipFocusLateralOffset = std::clamp(MATH::FiniteOr(focusLateralOffset, 0.0f), -5.0f, 5.0f);
    runtime_.relationshipFocusHeightOffset = std::clamp(MATH::FiniteOr(focusHeightOffset, 0.0f), -5.0f, 5.0f);
    runtime_.relationshipSuppressModifiers = suppressModifiers;
    runtime_.relationshipYawOffsetDegrees = std::clamp(MATH::FiniteOr(yawOffsetDegrees, 0.0f), -75.0f, 75.0f);
    runtime_.relationshipWeight = std::clamp(MATH::FiniteOr(weight, 0.0f), 0.0f, 1.0f);
    runtime_.relationshipWeightVelocity = MATH::FiniteOr(weightVelocity, 0.0f);
}

void CameraFollowComponent::SetRuntimeOrbit(float yawDegrees, float pitchDegrees, float desiredDistance,
                                            float resolvedDistance, float lookIdleSeconds) noexcept {
    runtime_.yawDegrees = MATH::FiniteOr(yawDegrees, Settings().initialYawDegrees);
    runtime_.pitchDegrees = std::clamp(MATH::FiniteOr(pitchDegrees, GetInitialPitchDegrees()),
                                       Settings().minimumPitchDegrees, Settings().maximumPitchDegrees);
    runtime_.desiredDistance = std::clamp(MATH::FiniteOr(desiredDistance, GetInitialDistance()),
                                          Settings().minimumDistance, Settings().maximumDistance);
    runtime_.distance =
        std::clamp(MATH::FiniteOr(resolvedDistance, runtime_.desiredDistance), 0.001f, Settings().maximumDistance);
    runtime_.lookIdleSeconds = NonNegativeFinite(lookIdleSeconds, 0.0f);
}

void CameraFollowComponent::SetRuntimeView(const MATH::Vec3& eye, const MATH::Vec3& lookAt,
                                           bool collisionLimited) noexcept {
    runtime_.eye = MATH::FiniteOr(eye, runtime_.eye);
    runtime_.lookAt = MATH::FiniteOr(lookAt, runtime_.lookAt);
    runtime_.collisionLimited = collisionLimited;
    runtime_.initialized = true;
}

void CameraFollowComponent::ResetRuntimeState() noexcept { runtime_ = {}; }

void CameraFollowComponent::ClampSettings() noexcept { settings_.Normalize(); }

} // namespace HIKARI
