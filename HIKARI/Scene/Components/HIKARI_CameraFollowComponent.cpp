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

        float FiniteOr(float value, float fallback) noexcept {
            return std::isfinite(value) ? value : fallback;
        }

        float NonNegativeFinite(
            float value,
            float fallback) noexcept {

            return (std::max)(FiniteOr(value, fallback), 0.0f);
        }

        MATH::Vec3 FiniteOr(
            const MATH::Vec3& value,
            const MATH::Vec3& fallback) noexcept {

            return MATH::IsFinite(value) ? value : fallback;
        }

        CAMERA::CameraTargetFramingDynamics SanitizeDynamics(
            const CAMERA::CameraTargetFramingDynamics& dynamics) noexcept {

            const CAMERA::CameraTargetFramingDynamics defaults{};
            CAMERA::CameraTargetFramingDynamics result = dynamics;
            result.enterSmoothSeconds = NonNegativeFinite(
                result.enterSmoothSeconds,
                defaults.enterSmoothSeconds);
            result.exitSmoothSeconds = NonNegativeFinite(
                result.exitSmoothSeconds,
                defaults.exitSmoothSeconds);
            result.targetSwitchSmoothSeconds = NonNegativeFinite(
                result.targetSwitchSmoothSeconds,
                defaults.targetSwitchSmoothSeconds);
            result.yawSmoothSeconds = NonNegativeFinite(
                result.yawSmoothSeconds,
                defaults.yawSmoothSeconds);
            result.pitchSmoothSeconds = NonNegativeFinite(
                result.pitchSmoothSeconds,
                defaults.pitchSmoothSeconds);
            result.focusSmoothSeconds = NonNegativeFinite(
                result.focusSmoothSeconds,
                defaults.focusSmoothSeconds);
            result.distanceExpandSmoothSeconds = NonNegativeFinite(
                result.distanceExpandSmoothSeconds,
                defaults.distanceExpandSmoothSeconds);
            result.distanceContractSmoothSeconds = NonNegativeFinite(
                result.distanceContractSmoothSeconds,
                defaults.distanceContractSmoothSeconds);
            result.yawDeadZoneDegrees = std::clamp(
                FiniteOr(
                    result.yawDeadZoneDegrees,
                    defaults.yawDeadZoneDegrees),
                0.0f,
                180.0f);
            return result;
        }


    }

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
        targetObjectId_.value = SCENE::SERIALIZATION::SceneObjectReferenceOr(
            in.value("targetObjectId", nlohmann::json{}), targetObjectId_.value);
        useOwnerAsFallbackTarget_ = in.value(
            "useOwnerAsFallbackTarget",
            useOwnerAsFallbackTarget_);
        evaluatedSettings_.reset();
        settings_.Deserialize(in);
        gameplayBinding_.profile.value = in.value("cameraProfile", std::string{});
        gameplayBinding_.initialMode = in.value("cameraMode", std::string{});
        gameplayBinding_.lookTarget.value = SCENE::SERIALIZATION::SceneObjectReferenceOr(in.value("cameraLookTarget", nlohmann::json{}), 0);
        gameplayBinding_.overrides = in.value("cameraOverrides", nlohmann::json::object());
        gameplayState_.Reset();
        ClampSettings();
        ResetRuntimeState();
    }

    float CameraFollowComponent::GetInitialYawDegrees() const noexcept {
        return Settings().initialYawDegrees;
    }

    float CameraFollowComponent::GetInitialPitchDegrees() const noexcept {
        return std::clamp(
            Settings().initialPitchDegrees,
            Settings().minimumPitchDegrees,
            Settings().maximumPitchDegrees);
    }

    float CameraFollowComponent::GetInitialDistance() const noexcept {
        return std::clamp(
            Settings().initialDistance,
            Settings().minimumDistance,
            Settings().maximumDistance);
    }

    float CameraFollowComponent::GetMinimumPitchDegrees() const noexcept {
        return Settings().minimumPitchDegrees;
    }

    float CameraFollowComponent::GetMaximumPitchDegrees() const noexcept {
        return Settings().maximumPitchDegrees;
    }

    float CameraFollowComponent::GetMinimumDistance() const noexcept {
        return Settings().minimumDistance;
    }

    float CameraFollowComponent::GetMaximumDistance() const noexcept {
        return Settings().maximumDistance;
    }

    float CameraFollowComponent::GetFollowSmooth() const noexcept {
        return Settings().followSmooth;
    }

    float CameraFollowComponent::GetLookSmooth() const noexcept {
        return Settings().lookSmooth;
    }

    float CameraFollowComponent::GetDistanceSmooth() const noexcept {
        return Settings().distanceSmooth;
    }

    float CameraFollowComponent::GetYawSpeedDegreesPerSecond() const noexcept {
        return Settings().yawSpeedDegreesPerSecond;
    }

    float CameraFollowComponent::GetPitchSpeedDegreesPerSecond() const noexcept {
        return Settings().pitchSpeedDegreesPerSecond;
    }

    float CameraFollowComponent::GetMouseSensitivityDegreesPerPixel() const noexcept {
        return Settings().mouseSensitivityDegreesPerPixel;
    }

    float CameraFollowComponent::GetZoomSpeedUnitsPerSecond() const noexcept {
        return Settings().zoomSpeedUnitsPerSecond;
    }

    float CameraFollowComponent::GetMouseWheelZoomUnitsPerStep() const noexcept {
        return Settings().mouseWheelZoomUnitsPerStep;
    }

    float CameraFollowComponent::GetAutoRecenterDelaySeconds() const noexcept {
        return Settings().autoRecenterDelaySeconds;
    }

    float CameraFollowComponent::GetAutoRecenterSpeedDegreesPerSecond() const noexcept {
        return Settings().autoRecenterSpeedDegreesPerSecond;
    }

    float CameraFollowComponent::GetCollisionRadius() const noexcept {
        return Settings().collisionRadius;
    }

    float CameraFollowComponent::GetCollisionPadding() const noexcept {
        return Settings().collisionPadding;
    }

    void CameraFollowComponent::InitializeRuntimeState(
        const MATH::Vec3& eye,
        const MATH::Vec3& lookAt) noexcept {

        runtimeYawDegrees_ = GetInitialYawDegrees();
        runtimePitchDegrees_ = GetInitialPitchDegrees();
        runtimeDistance_ = GetInitialDistance();
        runtimeDesiredDistance_ = runtimeDistance_;
        runtimeLookIdleSeconds_ = 0.0f;
        runtimeEye_ = eye;
        runtimeLookAt_ = lookAt;
        runtimeFollowPivot_ = lookAt;
        runtimeTargetPivot_ = lookAt;
        runtimeCollisionLimited_ = false;
        runtimeFramingInitialized_ = false;
        runtimeFramingHasSubmission_ = false;
        runtimeFramingTargetObject_ = {};
        runtimeFramingSourceId_ = 0u;
        runtimeFramingPriority_ = 0;
        runtimeFramingDynamics_ = {};
        runtimeFramingTargetWeight_ = 0.45f;
        runtimeFramingDistanceScale_ = 0.18f;
        runtimeFramingMaximumDistanceOffset_ = 2.0f;
        runtimeFramingYawTrackingSpeed_ = 540.0f;
        runtimeFramingPreferredPitchDegrees_ = 17.0f;
        runtimeFramingPitchTrackingSpeed_ = 240.0f;
        runtimeFramingSuppressOrbitInput_ = true;
        runtimeFramingSuppressZoomInput_ = true;
        runtimeFramingSwitchActive_ = false;
        runtimeFramingSwitchRemainingSeconds_ = 0.0f;
        runtimeFramingWeight_ = 0.0f;
        runtimeFramingWeightVelocity_ = 0.0f;
        runtimeFramingSecondaryPivot_ = {};
        runtimeFramingSecondaryPivotVelocity_ = {};
        runtimeFramingYawVelocity_ = 0.0f;
        runtimeFramingPitchVelocity_ = 0.0f;
        runtimeFramingFocusOffset_ = {};
        runtimeFramingFocusVelocity_ = {};
        runtimeFramingDistanceVelocity_ = 0.0f;
        runtimeRelationshipShotInitialized_ = false;
        runtimeRelationshipShotHasSubmission_ = false;
        runtimeRelationshipPreferredSide_ = 0.0f;
        runtimeRelationshipShoulderAngleDegrees_ = 0.0f;
        runtimeRelationshipVerticalFovOffsetRadians_ = 0.0f;
        runtimeRelationshipRollRadians_ = 0.0f;
        runtimeRelationshipSideClearanceBias_ = 0.0f;
        runtimeRelationshipDistanceOffset_ = 0.0f;
        runtimeRelationshipPivotHeightOffset_ = 0.0f;
        runtimeRelationshipFocusLateralOffset_ = 0.0f;
        runtimeRelationshipFocusHeightOffset_ = 0.0f;
        runtimeRelationshipSuppressModifiers_ = false;
        runtimeRelationshipYawOffsetDegrees_ = 0.0f;
        runtimeRelationshipWeight_ = 0.0f;
        runtimeRelationshipWeightVelocity_ = 0.0f;
        runtimeScreenCompositionActive_ = false;
        runtimeScreenCompositionSide_ = 1.0f;
        runtimeScreenCompositionPrimaryX_ = 0.5f;
        runtimeScreenCompositionPrimaryY_ = 0.5f;
        runtimeScreenCompositionSecondaryX_ = 0.5f;
        runtimeScreenCompositionSecondaryY_ = 0.5f;
        runtimeScreenCompositionSoftViolation_ = false;
        runtimeScreenCompositionHardViolation_ = false;
        runtimeScreenCompositionCollisionBlend_ = 0.0f;
        runtimeScreenCompositionCollisionBlendVelocity_ = 0.0f;
        runtimeInitialized_ = true;
    }

    void CameraFollowComponent::SetRuntimeTracking(
        const MATH::Vec3& followPivot,
        const MATH::Vec3& targetPivot) noexcept {

        runtimeFollowPivot_ = FiniteOr(
            followPivot,
            runtimeFollowPivot_);
        runtimeTargetPivot_ = FiniteOr(
            targetPivot,
            runtimeTargetPivot_);
    }

    void CameraFollowComponent::SetRuntimeFramingState(
        bool hasSubmission,
        RuntimeObjectHandle targetObject,
        CAMERA::CameraRigSourceId sourceId,
        int priority,
        const CAMERA::CameraTargetFramingDynamics& dynamics,
        float targetWeight,
        float distanceScale,
        float maximumDistanceOffset,
        float yawTrackingSpeed,
        float preferredPitchDegrees,
        float pitchTrackingSpeed,
        bool suppressOrbitInput,
        bool suppressZoomInput,
        bool switchActive,
        float switchRemainingSeconds,
        const MATH::Vec3& secondaryPivot,
        float framingWeight,
        float framingWeightVelocity,
        const MATH::Vec3& secondaryPivotVelocity,
        float yawVelocity,
        float pitchVelocity,
        const MATH::Vec3& focusOffset,
        const MATH::Vec3& focusVelocity,
        float distanceVelocity) noexcept {

        runtimeFramingInitialized_ = true;
        runtimeFramingHasSubmission_ = hasSubmission;
        runtimeFramingTargetObject_ = targetObject;
        runtimeFramingSourceId_ = sourceId;
        runtimeFramingPriority_ = priority;
        runtimeFramingDynamics_ = SanitizeDynamics(dynamics);
        runtimeFramingTargetWeight_ = std::clamp(
            FiniteOr(targetWeight, 0.0f),
            0.0f,
            1.0f);
        runtimeFramingDistanceScale_ = NonNegativeFinite(
            distanceScale,
            0.0f);
        runtimeFramingMaximumDistanceOffset_ = NonNegativeFinite(
            maximumDistanceOffset,
            0.0f);
        runtimeFramingYawTrackingSpeed_ = NonNegativeFinite(
            yawTrackingSpeed,
            0.0f);
        runtimeFramingPreferredPitchDegrees_ = FiniteOr(
            preferredPitchDegrees,
            17.0f);
        runtimeFramingPitchTrackingSpeed_ = NonNegativeFinite(
            pitchTrackingSpeed,
            0.0f);
        runtimeFramingSuppressOrbitInput_ = suppressOrbitInput;
        runtimeFramingSuppressZoomInput_ = suppressZoomInput;
        runtimeFramingSwitchActive_ = switchActive;
        runtimeFramingSwitchRemainingSeconds_ = NonNegativeFinite(
            switchRemainingSeconds,
            0.0f);
        runtimeFramingSecondaryPivot_ = FiniteOr(
            secondaryPivot,
            MATH::Vec3{});
        runtimeFramingWeight_ = std::clamp(
            FiniteOr(framingWeight, 0.0f),
            0.0f,
            1.0f);
        runtimeFramingWeightVelocity_ = FiniteOr(
            framingWeightVelocity,
            0.0f);
        runtimeFramingSecondaryPivotVelocity_ = FiniteOr(
            secondaryPivotVelocity,
            MATH::Vec3{});
        runtimeFramingYawVelocity_ = FiniteOr(yawVelocity, 0.0f);
        runtimeFramingPitchVelocity_ = FiniteOr(pitchVelocity, 0.0f);
        runtimeFramingFocusOffset_ = FiniteOr(
            focusOffset,
            MATH::Vec3{});
        runtimeFramingFocusVelocity_ = FiniteOr(
            focusVelocity,
            MATH::Vec3{});
        runtimeFramingDistanceVelocity_ = FiniteOr(
            distanceVelocity,
            0.0f);
    }

    void CameraFollowComponent::SetRuntimeScreenCompositionState(
        bool active,
        float side,
        float primaryX,
        float primaryY,
        float secondaryX,
        float secondaryY,
        bool softViolation,
        bool hardViolation,
        float collisionBlend,
        float collisionBlendVelocity) noexcept {

        runtimeScreenCompositionActive_ = active;
        runtimeScreenCompositionSide_ = side >= 0.0f ? 1.0f : -1.0f;
        runtimeScreenCompositionPrimaryX_ = FiniteOr(primaryX, 0.5f);
        runtimeScreenCompositionPrimaryY_ = FiniteOr(primaryY, 0.5f);
        runtimeScreenCompositionSecondaryX_ = FiniteOr(
            secondaryX,
            0.5f);
        runtimeScreenCompositionSecondaryY_ = FiniteOr(
            secondaryY,
            0.5f);
        runtimeScreenCompositionSoftViolation_ = softViolation;
        runtimeScreenCompositionHardViolation_ = hardViolation;
        runtimeScreenCompositionCollisionBlend_ = std::clamp(
            FiniteOr(collisionBlend, 0.0f),
            0.0f,
            1.0f);
        runtimeScreenCompositionCollisionBlendVelocity_ = FiniteOr(
            collisionBlendVelocity,
            0.0f);
    }

    void CameraFollowComponent::SetRuntimeRelationshipShotState(
        bool hasSubmission,
        float preferredSide,
        float shoulderAngleDegrees,
        float verticalFovOffsetRadians,
        float rollRadians,
        float sideClearanceBias,
        float distanceOffset,
        float pivotHeightOffset,
        float focusLateralOffset,
        float focusHeightOffset,
        bool suppressModifiers,
        float yawOffsetDegrees,
        float weight,
        float weightVelocity) noexcept {

        runtimeRelationshipShotInitialized_ = true;
        runtimeRelationshipShotHasSubmission_ = hasSubmission;
        runtimeRelationshipPreferredSide_ = std::clamp(
            FiniteOr(preferredSide, 0.0f),
            -1.0f,
            1.0f);
        runtimeRelationshipShoulderAngleDegrees_ = std::clamp(
            NonNegativeFinite(shoulderAngleDegrees, 0.0f),
            0.0f,
            75.0f);
        runtimeRelationshipVerticalFovOffsetRadians_ = std::clamp(
            FiniteOr(verticalFovOffsetRadians, 0.0f),
            -1.5f,
            1.5f);
        runtimeRelationshipRollRadians_ = std::clamp(
            FiniteOr(rollRadians, 0.0f),
            -3.14159265f,
            3.14159265f);
        runtimeRelationshipSideClearanceBias_ = std::clamp(
            NonNegativeFinite(sideClearanceBias, 0.0f),
            0.0f,
            5.0f);
        runtimeRelationshipDistanceOffset_ = std::clamp(
            FiniteOr(distanceOffset, 0.0f),
            -10.0f,
            10.0f);
        runtimeRelationshipPivotHeightOffset_ = std::clamp(
            FiniteOr(pivotHeightOffset, 0.0f),
            -5.0f,
            5.0f);
        runtimeRelationshipFocusLateralOffset_ = std::clamp(
            FiniteOr(focusLateralOffset, 0.0f),
            -5.0f,
            5.0f);
        runtimeRelationshipFocusHeightOffset_ = std::clamp(
            FiniteOr(focusHeightOffset, 0.0f),
            -5.0f,
            5.0f);
        runtimeRelationshipSuppressModifiers_ = suppressModifiers;
        runtimeRelationshipYawOffsetDegrees_ = std::clamp(
            FiniteOr(yawOffsetDegrees, 0.0f),
            -75.0f,
            75.0f);
        runtimeRelationshipWeight_ = std::clamp(
            FiniteOr(weight, 0.0f),
            0.0f,
            1.0f);
        runtimeRelationshipWeightVelocity_ = FiniteOr(
            weightVelocity,
            0.0f);
    }

    void CameraFollowComponent::SetRuntimeOrbit(
        float yawDegrees,
        float pitchDegrees,
        float desiredDistance,
        float resolvedDistance,
        float lookIdleSeconds) noexcept {

        runtimeYawDegrees_ = FiniteOr(
            yawDegrees,
            GetInitialYawDegrees());
        runtimePitchDegrees_ = std::clamp(
            FiniteOr(pitchDegrees, GetInitialPitchDegrees()),
            Settings().minimumPitchDegrees,
            Settings().maximumPitchDegrees);
        runtimeDesiredDistance_ = std::clamp(
            FiniteOr(desiredDistance, GetInitialDistance()),
            Settings().minimumDistance,
            Settings().maximumDistance);
        runtimeDistance_ = std::clamp(
            FiniteOr(resolvedDistance, runtimeDesiredDistance_),
            0.001f,
            Settings().maximumDistance);
        runtimeLookIdleSeconds_ = NonNegativeFinite(
            lookIdleSeconds,
            0.0f);
    }

    void CameraFollowComponent::SetRuntimeView(
        const MATH::Vec3& eye,
        const MATH::Vec3& lookAt,
        bool collisionLimited) noexcept {

        runtimeEye_ = FiniteOr(eye, runtimeEye_);
        runtimeLookAt_ = FiniteOr(lookAt, runtimeLookAt_);
        runtimeCollisionLimited_ = collisionLimited;
        runtimeInitialized_ = true;
    }

    void CameraFollowComponent::ResetRuntimeState() noexcept {
        runtimeInitialized_ = false;
        runtimeYawDegrees_ = 0.0f;
        runtimePitchDegrees_ = 0.0f;
        runtimeDistance_ = 0.0f;
        runtimeDesiredDistance_ = 0.0f;
        runtimeLookIdleSeconds_ = 0.0f;
        runtimeEye_ = {};
        runtimeLookAt_ = {};
        runtimeFollowPivot_ = {};
        runtimeTargetPivot_ = {};
        runtimeCollisionLimited_ = false;
        runtimeFramingInitialized_ = false;
        runtimeFramingHasSubmission_ = false;
        runtimeFramingTargetObject_ = {};
        runtimeFramingSourceId_ = 0u;
        runtimeFramingPriority_ = 0;
        runtimeFramingDynamics_ = {};
        runtimeFramingTargetWeight_ = 0.45f;
        runtimeFramingDistanceScale_ = 0.18f;
        runtimeFramingMaximumDistanceOffset_ = 2.0f;
        runtimeFramingYawTrackingSpeed_ = 540.0f;
        runtimeFramingPreferredPitchDegrees_ = 17.0f;
        runtimeFramingPitchTrackingSpeed_ = 240.0f;
        runtimeFramingSuppressOrbitInput_ = true;
        runtimeFramingSuppressZoomInput_ = true;
        runtimeFramingSwitchActive_ = false;
        runtimeFramingSwitchRemainingSeconds_ = 0.0f;
        runtimeFramingWeight_ = 0.0f;
        runtimeFramingWeightVelocity_ = 0.0f;
        runtimeFramingSecondaryPivot_ = {};
        runtimeFramingSecondaryPivotVelocity_ = {};
        runtimeFramingYawVelocity_ = 0.0f;
        runtimeFramingPitchVelocity_ = 0.0f;
        runtimeFramingFocusOffset_ = {};
        runtimeFramingFocusVelocity_ = {};
        runtimeFramingDistanceVelocity_ = 0.0f;
        runtimeRelationshipShotInitialized_ = false;
        runtimeRelationshipShotHasSubmission_ = false;
        runtimeRelationshipPreferredSide_ = 0.0f;
        runtimeRelationshipShoulderAngleDegrees_ = 0.0f;
        runtimeRelationshipVerticalFovOffsetRadians_ = 0.0f;
        runtimeRelationshipRollRadians_ = 0.0f;
        runtimeRelationshipSideClearanceBias_ = 0.0f;
        runtimeRelationshipDistanceOffset_ = 0.0f;
        runtimeRelationshipPivotHeightOffset_ = 0.0f;
        runtimeRelationshipFocusLateralOffset_ = 0.0f;
        runtimeRelationshipFocusHeightOffset_ = 0.0f;
        runtimeRelationshipSuppressModifiers_ = false;
        runtimeRelationshipYawOffsetDegrees_ = 0.0f;
        runtimeRelationshipWeight_ = 0.0f;
        runtimeRelationshipWeightVelocity_ = 0.0f;
        runtimeScreenCompositionActive_ = false;
        runtimeScreenCompositionSide_ = 1.0f;
        runtimeScreenCompositionPrimaryX_ = 0.5f;
        runtimeScreenCompositionPrimaryY_ = 0.5f;
        runtimeScreenCompositionSecondaryX_ = 0.5f;
        runtimeScreenCompositionSecondaryY_ = 0.5f;
        runtimeScreenCompositionSoftViolation_ = false;
        runtimeScreenCompositionHardViolation_ = false;
        runtimeScreenCompositionCollisionBlend_ = 0.0f;
        runtimeScreenCompositionCollisionBlendVelocity_ = 0.0f;
    }

    void CameraFollowComponent::ClampSettings() noexcept { settings_.Normalize(); }

} // namespace HIKARI
