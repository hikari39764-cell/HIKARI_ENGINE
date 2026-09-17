#include "Scene/Components/HIKARI_CameraActivationVolumeComponent.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"

#include <string>

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI {

    void CameraComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.Bool("Enabled", enabled_);
        builder.Float("Vertical FOV", verticalFovDegrees_);
        builder.Bool("Orthographic", orthographic_);
        if (orthographic_) {
            builder.Float("Vertical size", orthographicHeight_);
            builder.Bool("Snap camera to pixel grid", pixelSnap_);
            if (pixelSnap_) builder.Float("Pixels per world unit", pixelsPerUnit_);
        }
        builder.Float("Near Clip", nearClip_);
        builder.Float("Far Clip", farClip_);
        ClampLens();
    }

    void CameraActivationVolumeComponent::BuildInspector(
        IInspectorBuilder& builder) {

        builder.Bool("Enabled", enabled_);
        builder.SceneObjectIdPicker("Subject", subjectObjectId_);
        builder.SceneObjectIdPicker("Camera", cameraObjectId_);
        builder.Float("Half Extent X", halfExtents_.x);
        builder.Float("Half Extent Y", halfExtents_.y);
        builder.Float("Half Extent Z", halfExtents_.z);
        builder.Int("Activation Priority", priority_);
        builder.Float("Blend Seconds", blendSeconds_);
        builder.Bool("Affects Control Basis", affectsControlBasis_);
        ClampSettings();
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
        changed |= builder.Bool(
            "Camera Collision Static Only",
            collisionStaticOnly_);
        changed |= builder.Float("Collision Radius", collisionRadius_);
        changed |= builder.Float("Collision Padding", collisionPadding_);
        int collisionMask = static_cast<int>(collisionLayerMask_);
        if (builder.Int("Collision Layer Mask", collisionMask)) {
            collisionLayerMask_ = static_cast<uint32_t>(collisionMask);
            changed = true;
        }

        changed |= builder.Bool(
            "Screen-space Target Composition",
            screenCompositionEnabled_);
        changed |= builder.Float(
            "Primary Anchor X",
            screenCompositionPrimaryAnchorX_);
        changed |= builder.Float(
            "Primary Anchor Y",
            screenCompositionPrimaryAnchorY_);
        changed |= builder.Float(
            "Secondary Anchor X",
            screenCompositionSecondaryAnchorX_);
        changed |= builder.Float(
            "Secondary Anchor Y",
            screenCompositionSecondaryAnchorY_);
        changed |= builder.Float(
            "Soft Zone Half Width",
            screenCompositionSoftZoneHalfWidth_);
        changed |= builder.Float(
            "Soft Zone Half Height",
            screenCompositionSoftZoneHalfHeight_);
        changed |= builder.Float(
            "Hard Zone Half Width",
            screenCompositionHardZoneHalfWidth_);
        changed |= builder.Float(
            "Hard Zone Half Height",
            screenCompositionHardZoneHalfHeight_);
        changed |= builder.Float(
            "Preferred Shoulder Angle",
            screenCompositionPreferredShoulderAngleDegrees_);
        changed |= builder.Float(
            "Maximum Shoulder Angle",
            screenCompositionMaximumShoulderAngleDegrees_);
        changed |= builder.Float(
            "Composition Focus Height Bias",
            screenCompositionFocusHeightBias_);
        changed |= builder.Float(
            "Hard-zone Response Scale",
            screenCompositionHardResponseScale_);
        changed |= builder.Float(
            "Collision Centering Strength",
            screenCompositionCollisionCenteringStrength_);

        if (runtimeScreenCompositionActive_) {
            builder.Text(
                "Runtime composition P(" +
                std::to_string(runtimeScreenCompositionPrimaryX_) + ", " +
                std::to_string(runtimeScreenCompositionPrimaryY_) +
                ") T(" +
                std::to_string(runtimeScreenCompositionSecondaryX_) + ", " +
                std::to_string(runtimeScreenCompositionSecondaryY_) + ")");
            builder.Text(
                std::string("Runtime zone: ") +
                (runtimeScreenCompositionHardViolation_
                    ? "hard correction"
                    : (runtimeScreenCompositionSoftViolation_
                        ? "soft correction"
                        : "stable")) +
                " | collision blend=" +
                std::to_string(runtimeScreenCompositionCollisionBlend_));
        }

        if (changed) {
            ClampSettings();
            ResetRuntimeState();
        }
    }

} // namespace HIKARI
