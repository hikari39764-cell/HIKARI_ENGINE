#include "HIKARI_CameraFollowComponent.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI {

    namespace JsonMath = SERIALIZATION::JSON::MATH;

    namespace {
        constexpr float kMinimumRange = 0.01f;

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

    void CameraFollowComponent::BuildInspector(
        IInspectorBuilder& builder) {

        bool changed = false;
        changed |= builder.Bool("Enabled", enabled_);
        changed |= builder.Int("Rig Priority", priority_);
        changed |= builder.SceneObjectIdPicker(
            "Follow Target",
            targetObjectId_);
        changed |= builder.Bool(
            "Use Parent as Target Fallback",
            useOwnerAsFallbackTarget_);

        changed |= builder.Float("Pivot Offset X", pivotOffset_.x);
        changed |= builder.Float("Pivot Offset Y", pivotOffset_.y);
        changed |= builder.Float("Pivot Offset Z", pivotOffset_.z);
        changed |= builder.Float("Initial Yaw", initialYawDegrees_);
        changed |= builder.Float("Initial Pitch", initialPitchDegrees_);
        changed |= builder.Float("Initial Distance", initialDistance_);
        changed |= builder.Float("Minimum Pitch", minimumPitchDegrees_);
        changed |= builder.Float("Maximum Pitch", maximumPitchDegrees_);
        changed |= builder.Float("Minimum Distance", minimumDistance_);
        changed |= builder.Float("Maximum Distance", maximumDistance_);
        changed |= builder.Float("Follow Smooth", followSmooth_);
        changed |= builder.Float("Look Smooth", lookSmooth_);
        changed |= builder.Float("Distance Smooth", distanceSmooth_);

        changed |= builder.Bool("Enable Orbit Input", orbitInputEnabled_);
        changed |= builder.InputActionIdPicker(
            "Look Action",
            INPUT::InputActionValueType::Axis2D,
            lookActionId_);
        changed |= builder.InputActionIdPicker(
            "Zoom Action",
            INPUT::InputActionValueType::Axis1D,
            zoomActionId_);
        changed |= builder.InputActionIdPicker(
            "Recenter Action",
            INPUT::InputActionValueType::Button,
            recenterActionId_);
        changed |= builder.Float(
            "Gamepad Yaw Speed (deg/s)",
            yawSpeedDegreesPerSecond_);
        changed |= builder.Float(
            "Gamepad Pitch Speed (deg/s)",
            pitchSpeedDegreesPerSecond_);
        changed |= builder.Float(
            "Mouse Sensitivity (deg/pixel)",
            mouseSensitivityDegreesPerPixel_);
        changed |= builder.Float(
            "Gamepad Zoom Speed (units/s)",
            zoomSpeedUnitsPerSecond_);
        changed |= builder.Float(
            "Mouse Wheel Zoom Step",
            mouseWheelZoomUnitsPerStep_);
        changed |= builder.Bool("Invert Vertical Look", invertVerticalLook_);
        changed |= builder.Bool("Auto Recenter", autoRecenterEnabled_);
        changed |= builder.Float(
            "Recenter Delay",
            autoRecenterDelaySeconds_);
        changed |= builder.Float(
            "Recenter Speed (deg/s)",
            autoRecenterSpeedDegreesPerSecond_);

        changed |= builder.Bool("Camera Collision", collisionEnabled_);
        changed |= builder.Float("Collision Radius", collisionRadius_);
        changed |= builder.Float("Collision Padding", collisionPadding_);
        int collisionMask = static_cast<int>(collisionLayerMask_);
        if (builder.Int("Collision Layer Mask", collisionMask)) {
            collisionLayerMask_ = static_cast<uint32_t>(collisionMask);
            changed = true;
        }

        if (changed) {
            ClampSettings();
            ResetRuntimeState();
        }
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
        runtimeInitialized_ = true;
    }

    void CameraFollowComponent::SetRuntimeTracking(
        const MATH::Vec3& followPivot,
        const MATH::Vec3& targetPivot) noexcept {

        runtimeFollowPivot_ = followPivot;
        runtimeTargetPivot_ = targetPivot;
    }

    void CameraFollowComponent::SetRuntimeOrbit(
        float yawDegrees,
        float pitchDegrees,
        float desiredDistance,
        float resolvedDistance,
        float lookIdleSeconds) noexcept {

        runtimeYawDegrees_ = yawDegrees;
        runtimePitchDegrees_ = pitchDegrees;
        runtimeDesiredDistance_ = desiredDistance;
        runtimeDistance_ = resolvedDistance;
        runtimeLookIdleSeconds_ = (std::max)(lookIdleSeconds, 0.0f);
    }

    void CameraFollowComponent::SetRuntimeView(
        const MATH::Vec3& eye,
        const MATH::Vec3& lookAt,
        bool collisionLimited) noexcept {

        runtimeEye_ = eye;
        runtimeLookAt_ = lookAt;
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
    }

    void CameraFollowComponent::ClampSettings() noexcept {
        if (!std::isfinite(initialYawDegrees_)) {
            initialYawDegrees_ = 0.0f;
        }
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
        followSmooth_ = (std::max)(followSmooth_, 0.0f);
        lookSmooth_ = (std::max)(lookSmooth_, 0.0f);
        distanceSmooth_ = (std::max)(distanceSmooth_, 0.0f);
        yawSpeedDegreesPerSecond_ =
            (std::max)(yawSpeedDegreesPerSecond_, 0.0f);
        pitchSpeedDegreesPerSecond_ =
            (std::max)(pitchSpeedDegreesPerSecond_, 0.0f);
        mouseSensitivityDegreesPerPixel_ =
            (std::max)(mouseSensitivityDegreesPerPixel_, 0.0f);
        zoomSpeedUnitsPerSecond_ =
            (std::max)(zoomSpeedUnitsPerSecond_, 0.0f);
        mouseWheelZoomUnitsPerStep_ =
            (std::max)(mouseWheelZoomUnitsPerStep_, 0.0f);
        autoRecenterDelaySeconds_ =
            (std::max)(autoRecenterDelaySeconds_, 0.0f);
        autoRecenterSpeedDegreesPerSecond_ =
            (std::max)(autoRecenterSpeedDegreesPerSecond_, 0.0f);
        collisionRadius_ = (std::max)(collisionRadius_, 0.01f);
        collisionPadding_ = (std::max)(collisionPadding_, 0.0f);
    }

} // namespace HIKARI
