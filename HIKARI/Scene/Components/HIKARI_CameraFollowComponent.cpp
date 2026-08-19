#include "HIKARI_CameraFollowComponent.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
namespace HIKARI {

    namespace JsonMath = SERIALIZATION::JSON::MATH;

    namespace {
        constexpr float kMinimumRange = 0.01f;

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

        float RadiansToDegrees(float radians) noexcept {
            return radians * 180.0f / std::numbers::pi_v<float>;
        }
    }

    void CameraFollowComponent::Serialize(nlohmann::json& out) const {
        out["enabled"] = enabled_;
        out["priority"] = priority_;
        out["targetObjectId"] = targetObjectId_.value;
        out["useOwnerAsFallbackTarget"] = useOwnerAsFallbackTarget_;
        out["pivotOffset"] = JsonMath::ToJsonArray(pivotOffset_);
        out["initialYawDegrees"] = initialYawDegrees_;
        out["initialPitchDegrees"] = initialPitchDegrees_;
        out["initialDistance"] = initialDistance_;
        out["minimumPitchDegrees"] = minimumPitchDegrees_;
        out["maximumPitchDegrees"] = maximumPitchDegrees_;
        out["minimumDistance"] = minimumDistance_;
        out["maximumDistance"] = maximumDistance_;
        out["followSmooth"] = followSmooth_;
        out["lookSmooth"] = lookSmooth_;
        out["distanceSmooth"] = distanceSmooth_;
        out["orbitInputEnabled"] = orbitInputEnabled_;
        out["lookAction"] = lookActionId_;
        out["zoomAction"] = zoomActionId_;
        out["recenterAction"] = recenterActionId_;
        out["yawSpeedDegreesPerSecond"] = yawSpeedDegreesPerSecond_;
        out["pitchSpeedDegreesPerSecond"] = pitchSpeedDegreesPerSecond_;
        out["mouseSensitivityDegreesPerPixel"] =
            mouseSensitivityDegreesPerPixel_;
        out["zoomSpeedUnitsPerSecond"] = zoomSpeedUnitsPerSecond_;
        out["mouseWheelZoomUnitsPerStep"] =
            mouseWheelZoomUnitsPerStep_;
        out["invertVerticalLook"] = invertVerticalLook_;
        out["autoRecenterEnabled"] = autoRecenterEnabled_;
        out["autoRecenterDelaySeconds"] = autoRecenterDelaySeconds_;
        out["autoRecenterSpeedDegreesPerSecond"] =
            autoRecenterSpeedDegreesPerSecond_;
        out["collisionEnabled"] = collisionEnabled_;
        out["collisionRadius"] = collisionRadius_;
        out["collisionPadding"] = collisionPadding_;
        out["collisionLayerMask"] = collisionLayerMask_;
    }

    void CameraFollowComponent::Deserialize(const nlohmann::json& in) {
        enabled_ = in.value("enabled", enabled_);
        priority_ = in.value("priority", priority_);
        targetObjectId_.value = in.value(
            "targetObjectId",
            targetObjectId_.value);
        useOwnerAsFallbackTarget_ = in.value(
            "useOwnerAsFallbackTarget",
            useOwnerAsFallbackTarget_);
        if (in.contains("pivotOffset")) {
            pivotOffset_ = JSONREAD::Vec3Or(
                in["pivotOffset"],
                pivotOffset_);
        } else if (in.contains("lookAtOffset")) {
            pivotOffset_ = JSONREAD::Vec3Or(
                in["lookAtOffset"],
                pivotOffset_);
        }

        const bool hasOrbitSettings =
            in.contains("initialYawDegrees") ||
            in.contains("initialPitchDegrees") ||
            in.contains("initialDistance");
        initialYawDegrees_ = in.value(
            "initialYawDegrees",
            initialYawDegrees_);
        initialPitchDegrees_ = in.value(
            "initialPitchDegrees",
            initialPitchDegrees_);
        initialDistance_ = in.value(
            "initialDistance",
            initialDistance_);
        if (!hasOrbitSettings && in.contains("offset")) {
            const MATH::Vec3 legacyEye = JSONREAD::Vec3Or(
                in["offset"],
                MATH::Vec3{ 0.0f, 5.5f, -7.5f });
            const MATH::Vec3 delta = legacyEye - pivotOffset_;
            const float distance = MATH::Length(delta);
            if (distance > kMinimumRange) {
                initialDistance_ = distance;
                initialPitchDegrees_ = RadiansToDegrees(std::asin(
                    std::clamp(delta.y / distance, -1.0f, 1.0f)));
                initialYawDegrees_ = RadiansToDegrees(
                    std::atan2(delta.x, -delta.z));
            }
        }

        minimumPitchDegrees_ = in.value(
            "minimumPitchDegrees",
            minimumPitchDegrees_);
        maximumPitchDegrees_ = in.value(
            "maximumPitchDegrees",
            maximumPitchDegrees_);
        minimumDistance_ = in.value(
            "minimumDistance",
            minimumDistance_);
        maximumDistance_ = in.value(
            "maximumDistance",
            maximumDistance_);
        followSmooth_ = in.value("followSmooth", followSmooth_);
        lookSmooth_ = in.value("lookSmooth", lookSmooth_);
        distanceSmooth_ = in.value("distanceSmooth", distanceSmooth_);
        orbitInputEnabled_ = in.value(
            "orbitInputEnabled",
            orbitInputEnabled_);
        lookActionId_ = in.value("lookAction", lookActionId_);
        zoomActionId_ = in.value("zoomAction", zoomActionId_);
        recenterActionId_ = in.value(
            "recenterAction",
            recenterActionId_);
        yawSpeedDegreesPerSecond_ = in.value(
            "yawSpeedDegreesPerSecond",
            yawSpeedDegreesPerSecond_);
        pitchSpeedDegreesPerSecond_ = in.value(
            "pitchSpeedDegreesPerSecond",
            pitchSpeedDegreesPerSecond_);
        mouseSensitivityDegreesPerPixel_ = in.value(
            "mouseSensitivityDegreesPerPixel",
            mouseSensitivityDegreesPerPixel_);
        zoomSpeedUnitsPerSecond_ = in.value(
            "zoomSpeedUnitsPerSecond",
            zoomSpeedUnitsPerSecond_);
        mouseWheelZoomUnitsPerStep_ = in.value(
            "mouseWheelZoomUnitsPerStep",
            mouseWheelZoomUnitsPerStep_);
        invertVerticalLook_ = in.value(
            "invertVerticalLook",
            invertVerticalLook_);
        autoRecenterEnabled_ = in.value(
            "autoRecenterEnabled",
            autoRecenterEnabled_);
        autoRecenterDelaySeconds_ = in.value(
            "autoRecenterDelaySeconds",
            autoRecenterDelaySeconds_);
        autoRecenterSpeedDegreesPerSecond_ = in.value(
            "autoRecenterSpeedDegreesPerSecond",
            autoRecenterSpeedDegreesPerSecond_);
        collisionEnabled_ = in.value(
            "collisionEnabled",
            collisionEnabled_);
        collisionRadius_ = in.value(
            "collisionRadius",
            collisionRadius_);
        collisionPadding_ = in.value(
            "collisionPadding",
            collisionPadding_);
        collisionLayerMask_ = in.value(
            "collisionLayerMask",
            collisionLayerMask_);

        ClampSettings();
        ResetRuntimeState();
    }

    float CameraFollowComponent::GetInitialYawDegrees() const noexcept {
        return initialYawDegrees_;
    }

    float CameraFollowComponent::GetInitialPitchDegrees() const noexcept {
        return std::clamp(
            initialPitchDegrees_,
            minimumPitchDegrees_,
            maximumPitchDegrees_);
    }

    float CameraFollowComponent::GetInitialDistance() const noexcept {
        return std::clamp(
            initialDistance_,
            minimumDistance_,
            maximumDistance_);
    }

    float CameraFollowComponent::GetMinimumPitchDegrees() const noexcept {
        return minimumPitchDegrees_;
    }

    float CameraFollowComponent::GetMaximumPitchDegrees() const noexcept {
        return maximumPitchDegrees_;
    }

    float CameraFollowComponent::GetMinimumDistance() const noexcept {
        return minimumDistance_;
    }

    float CameraFollowComponent::GetMaximumDistance() const noexcept {
        return maximumDistance_;
    }

    float CameraFollowComponent::GetFollowSmooth() const noexcept {
        return followSmooth_;
    }

    float CameraFollowComponent::GetLookSmooth() const noexcept {
        return lookSmooth_;
    }

    float CameraFollowComponent::GetDistanceSmooth() const noexcept {
        return distanceSmooth_;
    }

    float CameraFollowComponent::GetYawSpeedDegreesPerSecond() const noexcept {
        return yawSpeedDegreesPerSecond_;
    }

    float CameraFollowComponent::GetPitchSpeedDegreesPerSecond() const noexcept {
        return pitchSpeedDegreesPerSecond_;
    }

    float CameraFollowComponent::GetMouseSensitivityDegreesPerPixel() const noexcept {
        return mouseSensitivityDegreesPerPixel_;
    }

    float CameraFollowComponent::GetZoomSpeedUnitsPerSecond() const noexcept {
        return zoomSpeedUnitsPerSecond_;
    }

    float CameraFollowComponent::GetMouseWheelZoomUnitsPerStep() const noexcept {
        return mouseWheelZoomUnitsPerStep_;
    }

    float CameraFollowComponent::GetAutoRecenterDelaySeconds() const noexcept {
        return autoRecenterDelaySeconds_;
    }

    float CameraFollowComponent::GetAutoRecenterSpeedDegreesPerSecond() const noexcept {
        return autoRecenterSpeedDegreesPerSecond_;
    }

    float CameraFollowComponent::GetCollisionRadius() const noexcept {
        return collisionRadius_;
    }

    float CameraFollowComponent::GetCollisionPadding() const noexcept {
        return collisionPadding_;
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
            minimumPitchDegrees_,
            maximumPitchDegrees_);
        runtimeDesiredDistance_ = std::clamp(
            FiniteOr(desiredDistance, GetInitialDistance()),
            minimumDistance_,
            maximumDistance_);
        runtimeDistance_ = std::clamp(
            FiniteOr(resolvedDistance, runtimeDesiredDistance_),
            minimumDistance_,
            maximumDistance_);
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
    }

    void CameraFollowComponent::ClampSettings() noexcept {
        pivotOffset_ = FiniteOr(
            pivotOffset_,
            MATH::Vec3{ 0.0f, 1.2f, 0.0f });
        initialYawDegrees_ = FiniteOr(initialYawDegrees_, 0.0f);
        minimumPitchDegrees_ = FiniteOr(minimumPitchDegrees_, -65.0f);
        maximumPitchDegrees_ = FiniteOr(maximumPitchDegrees_, 75.0f);
        initialPitchDegrees_ = FiniteOr(initialPitchDegrees_, 28.0f);
        minimumDistance_ = FiniteOr(minimumDistance_, 1.0f);
        maximumDistance_ = FiniteOr(maximumDistance_, 20.0f);
        initialDistance_ = FiniteOr(initialDistance_, 8.0f);
        minimumPitchDegrees_ = std::clamp(
            minimumPitchDegrees_,
            -89.0f,
            89.0f);
        maximumPitchDegrees_ = std::clamp(
            maximumPitchDegrees_,
            minimumPitchDegrees_,
            89.0f);
        initialPitchDegrees_ = std::clamp(
            initialPitchDegrees_,
            minimumPitchDegrees_,
            maximumPitchDegrees_);
        minimumDistance_ = (std::max)(minimumDistance_, kMinimumRange);
        maximumDistance_ = (std::max)(maximumDistance_, minimumDistance_);
        initialDistance_ = std::clamp(
            initialDistance_,
            minimumDistance_,
            maximumDistance_);
        followSmooth_ = NonNegativeFinite(followSmooth_, 12.0f);
        lookSmooth_ = NonNegativeFinite(lookSmooth_, 14.0f);
        distanceSmooth_ = NonNegativeFinite(distanceSmooth_, 18.0f);
        yawSpeedDegreesPerSecond_ = NonNegativeFinite(
            yawSpeedDegreesPerSecond_,
            180.0f);
        pitchSpeedDegreesPerSecond_ = NonNegativeFinite(
            pitchSpeedDegreesPerSecond_,
            140.0f);
        mouseSensitivityDegreesPerPixel_ = NonNegativeFinite(
            mouseSensitivityDegreesPerPixel_,
            0.12f);
        zoomSpeedUnitsPerSecond_ = NonNegativeFinite(
            zoomSpeedUnitsPerSecond_,
            30.0f);
        mouseWheelZoomUnitsPerStep_ = NonNegativeFinite(
            mouseWheelZoomUnitsPerStep_,
            1.0f);
        autoRecenterDelaySeconds_ = NonNegativeFinite(
            autoRecenterDelaySeconds_,
            1.25f);
        autoRecenterSpeedDegreesPerSecond_ = NonNegativeFinite(
            autoRecenterSpeedDegreesPerSecond_,
            90.0f);
        collisionRadius_ = (std::max)(
            NonNegativeFinite(collisionRadius_, 0.25f),
            0.01f);
        collisionPadding_ = NonNegativeFinite(collisionPadding_, 0.08f);
    }

} // namespace HIKARI
