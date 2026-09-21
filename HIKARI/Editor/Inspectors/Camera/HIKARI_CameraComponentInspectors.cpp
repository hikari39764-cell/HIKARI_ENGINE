#include "Scene/Components/HIKARI_CameraActivationVolumeComponent.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"

#include <string>

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI {

    void CameraComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled", ""}, enabled_);
        builder.Float(AuthoringFieldDescriptor{"verticalFovDegrees", "Vertical FOV", "Vertical field of view in degrees."}, verticalFovDegrees_);
        builder.Bool(AuthoringFieldDescriptor{"orthographic", "Orthographic", ""}, orthographic_);
        if (orthographic_) {
            builder.Float(AuthoringFieldDescriptor{"orthographicHeight", "Vertical size", ""}, orthographicHeight_);
            builder.Bool(AuthoringFieldDescriptor{"pixelSnap", "Snap camera to pixel grid", ""}, pixelSnap_);
            if (pixelSnap_) builder.Float(AuthoringFieldDescriptor{"pixelsPerUnit", "Pixels per world unit", ""}, pixelsPerUnit_);
        }
        builder.Float(AuthoringFieldDescriptor{"nearClip", "Near Clip", "Near clipping distance in world units."}, nearClip_);
        builder.Float(AuthoringFieldDescriptor{"farClip", "Far Clip", "Far clipping distance in world units."}, farClip_);
        ClampLens();
    }

    void CameraActivationVolumeComponent::BuildInspector(
        IInspectorBuilder& builder) {

        builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled", ""}, enabled_);
        builder.SceneObjectIdPicker(AuthoringFieldDescriptor{"subjectObjectId", "Subject", ""}, subjectObjectId_);
        builder.SceneObjectIdPicker(AuthoringFieldDescriptor{"cameraObjectId", "Camera", ""}, cameraObjectId_);
        builder.Float(AuthoringFieldDescriptor{"halfExtents.x", "Half Extent X", ""}, halfExtents_.x);
        builder.Float(AuthoringFieldDescriptor{"halfExtents.y", "Half Extent Y", ""}, halfExtents_.y);
        builder.Float(AuthoringFieldDescriptor{"halfExtents.z", "Half Extent Z", ""}, halfExtents_.z);
        builder.Int(AuthoringFieldDescriptor{"priority", "Activation Priority", ""}, priority_);
        builder.Float(AuthoringFieldDescriptor{"blendSeconds", "Blend Seconds", ""}, blendSeconds_);
        builder.Bool(AuthoringFieldDescriptor{"affectsControlBasis", "Affects Control Basis", ""}, affectsControlBasis_);
        ClampSettings();
    }

    void CameraFollowComponent::BuildInspector(
        IInspectorBuilder& builder) {

        bool changed = false;
        changed |= builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled", ""}, enabled_);
        changed |= builder.Int(AuthoringFieldDescriptor{"priority", "Rig Priority", ""}, priority_);
        changed |= builder.SceneObjectIdPicker(
            AuthoringFieldDescriptor{"targetObjectId", "Follow Target", ""},
            targetObjectId_);
        changed |= builder.Bool(
            AuthoringFieldDescriptor{"useOwnerAsFallbackTarget", "Use Parent as Target Fallback", ""},
            useOwnerAsFallbackTarget_);

        changed |= builder.Float(AuthoringFieldDescriptor{"pivotOffset.x", "Pivot Offset X", ""}, pivotOffset_.x);
        changed |= builder.Float(AuthoringFieldDescriptor{"pivotOffset.y", "Pivot Offset Y", ""}, pivotOffset_.y);
        changed |= builder.Float(AuthoringFieldDescriptor{"pivotOffset.z", "Pivot Offset Z", ""}, pivotOffset_.z);
        changed |= builder.Float(AuthoringFieldDescriptor{"initialYawDegrees", "Initial Yaw", ""}, initialYawDegrees_);
        changed |= builder.Float(AuthoringFieldDescriptor{"initialPitchDegrees", "Initial Pitch", ""}, initialPitchDegrees_);
        changed |= builder.Float(AuthoringFieldDescriptor{"initialDistance", "Initial Distance", ""}, initialDistance_);
        changed |= builder.Float(AuthoringFieldDescriptor{"minimumPitchDegrees", "Minimum Pitch", ""}, minimumPitchDegrees_);
        changed |= builder.Float(AuthoringFieldDescriptor{"maximumPitchDegrees", "Maximum Pitch", ""}, maximumPitchDegrees_);
        changed |= builder.Float(AuthoringFieldDescriptor{"minimumDistance", "Minimum Distance", ""}, minimumDistance_);
        changed |= builder.Float(AuthoringFieldDescriptor{"maximumDistance", "Maximum Distance", ""}, maximumDistance_);
        changed |= builder.Float(AuthoringFieldDescriptor{"followSmooth", "Follow Smooth", ""}, followSmooth_);
        changed |= builder.Float(AuthoringFieldDescriptor{"lookSmooth", "Look Smooth", ""}, lookSmooth_);
        changed |= builder.Float(AuthoringFieldDescriptor{"distanceSmooth", "Distance Smooth", ""}, distanceSmooth_);

        changed |= builder.Bool(AuthoringFieldDescriptor{"orbitInputEnabled", "Enable Orbit Input", ""}, orbitInputEnabled_);
        changed |= builder.InputActionIdPicker(
            AuthoringFieldDescriptor{"lookActionId", "Look Action", ""},
            INPUT::InputActionValueType::Axis2D,
            lookActionId_);
        changed |= builder.InputActionIdPicker(
            AuthoringFieldDescriptor{"zoomActionId", "Zoom Action", ""},
            INPUT::InputActionValueType::Axis1D,
            zoomActionId_);
        changed |= builder.InputActionIdPicker(
            AuthoringFieldDescriptor{"recenterActionId", "Recenter Action", ""},
            INPUT::InputActionValueType::Button,
            recenterActionId_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"yawSpeedDegreesPerSecond", "Gamepad Yaw Speed (deg/s)", ""},
            yawSpeedDegreesPerSecond_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"pitchSpeedDegreesPerSecond", "Gamepad Pitch Speed (deg/s)", ""},
            pitchSpeedDegreesPerSecond_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"mouseSensitivityDegreesPerPixel", "Mouse Sensitivity (deg/pixel)", ""},
            mouseSensitivityDegreesPerPixel_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"zoomSpeedUnitsPerSecond", "Gamepad Zoom Speed (units/s)", ""},
            zoomSpeedUnitsPerSecond_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"mouseWheelZoomUnitsPerStep", "Mouse Wheel Zoom Step", ""},
            mouseWheelZoomUnitsPerStep_);
        changed |= builder.Bool(AuthoringFieldDescriptor{"invertVerticalLook", "Invert Vertical Look", ""}, invertVerticalLook_);
        changed |= builder.Bool(AuthoringFieldDescriptor{"autoRecenterEnabled", "Auto Recenter", ""}, autoRecenterEnabled_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"autoRecenterDelaySeconds", "Recenter Delay", ""},
            autoRecenterDelaySeconds_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"autoRecenterSpeedDegreesPerSecond", "Recenter Speed (deg/s)", ""},
            autoRecenterSpeedDegreesPerSecond_);

        changed |= builder.Bool(AuthoringFieldDescriptor{"collisionEnabled", "Camera Collision", ""}, collisionEnabled_);
        changed |= builder.Bool(
            AuthoringFieldDescriptor{"collisionStaticOnly", "Camera Collision Static Only", ""},
            collisionStaticOnly_);
        changed |= builder.Float(AuthoringFieldDescriptor{"collisionRadius", "Collision Radius", ""}, collisionRadius_);
        changed |= builder.Float(AuthoringFieldDescriptor{"collisionPadding", "Collision Padding", ""}, collisionPadding_);
        int collisionMask = static_cast<int>(collisionLayerMask_);
        if (builder.Int(AuthoringFieldDescriptor{"collisionLayerMask", "Collision Layer Mask", ""}, collisionMask)) {
            collisionLayerMask_ = static_cast<uint32_t>(collisionMask);
            changed = true;
        }

        changed |= builder.Bool(
            AuthoringFieldDescriptor{"screenCompositionEnabled", "Screen-space Target Composition", ""},
            screenCompositionEnabled_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"screenCompositionPrimaryAnchorX", "Primary Anchor X", ""},
            screenCompositionPrimaryAnchorX_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"screenCompositionPrimaryAnchorY", "Primary Anchor Y", ""},
            screenCompositionPrimaryAnchorY_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"screenCompositionSecondaryAnchorX", "Secondary Anchor X", ""},
            screenCompositionSecondaryAnchorX_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"screenCompositionSecondaryAnchorY", "Secondary Anchor Y", ""},
            screenCompositionSecondaryAnchorY_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"screenCompositionSoftZoneHalfWidth", "Soft Zone Half Width", ""},
            screenCompositionSoftZoneHalfWidth_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"screenCompositionSoftZoneHalfHeight", "Soft Zone Half Height", ""},
            screenCompositionSoftZoneHalfHeight_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"screenCompositionHardZoneHalfWidth", "Hard Zone Half Width", ""},
            screenCompositionHardZoneHalfWidth_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"screenCompositionHardZoneHalfHeight", "Hard Zone Half Height", ""},
            screenCompositionHardZoneHalfHeight_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"screenCompositionPreferredShoulderAngleDegrees", "Preferred Shoulder Angle", ""},
            screenCompositionPreferredShoulderAngleDegrees_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"screenCompositionMaximumShoulderAngleDegrees", "Maximum Shoulder Angle", ""},
            screenCompositionMaximumShoulderAngleDegrees_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"screenCompositionFocusHeightBias", "Composition Focus Height Bias", ""},
            screenCompositionFocusHeightBias_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"screenCompositionHardResponseScale", "Hard-zone Response Scale", ""},
            screenCompositionHardResponseScale_);
        changed |= builder.Float(
            AuthoringFieldDescriptor{"screenCompositionCollisionCenteringStrength", "Collision Centering Strength", ""},
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
