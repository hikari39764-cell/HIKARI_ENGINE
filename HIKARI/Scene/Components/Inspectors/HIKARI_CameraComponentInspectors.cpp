// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/HIKARI_CameraFlowComponent.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Scene/Components/HIKARI_CameraActivationVolumeComponent.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"

#include <limits>
#include <string>
#include "Assets/HIKARI_AssetTypes.h"

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI {

void CameraFlowComponent::BuildInspector(IInspectorBuilder& builder) {
    auto field = [](const char* id, const char* key) {
        return AuthoringFieldDescriptor{id, TEXT::Localize(key).c_str(), ""};
    };
    builder.Bool(field("enabled", "cf.enabled"), settings_.enabled);
    builder.SceneObjectIdPicker(field("subjectObjectId", "gc.target"), settings_.subjectObjectId);
    builder.SceneObjectIdPicker(field("cameraObjectId", "gc.camera"), settings_.cameraObjectId);
    builder.AssetIdPicker(field("sequenceAssetGuid", "cf.sequence"), AssetType::Sequence, settings_.sequence.value);
    builder.Bool(field("triggerOnExit", "cf.onExit"), settings_.triggerOnExit);
    builder.String(field("requiredMode", "cf.requiredMode"), settings_.requiredMode);
    builder.String(field("afterMode", "cf.after"), settings_.afterMode);
    builder.Float(field("halfExtents.x", "cf.extentX"), settings_.halfExtents.x);
    builder.Float(field("halfExtents.y", "cf.extentY"), settings_.halfExtents.y);
    builder.Float(field("halfExtents.z", "cf.extentZ"), settings_.halfExtents.z);
    builder.Int(field("priority", "gc.priority"), settings_.priority);
    builder.Bool(field("repeatOnReentry", "cf.repeat"), settings_.repeatOnReentry);
    builder.Bool(field("finishOnInterrupt", "cf.finishInterrupt"), settings_.finishOnInterrupt);
}
void CameraComponent::BuildInspector(IInspectorBuilder& builder) {
    builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled", ""}, enabled_);
    builder.Float(AuthoringFieldDescriptor{"verticalFovDegrees", "Vertical FOV", "Vertical field of view in degrees."},
                  verticalFovDegrees_);
    builder.Bool(AuthoringFieldDescriptor{"orthographic", "Orthographic", ""}, orthographic_);
    if (orthographic_) {
        builder.Float(AuthoringFieldDescriptor{"orthographicHeight", "Vertical size", ""}, orthographicHeight_);
        builder.Bool(AuthoringFieldDescriptor{"pixelSnap", "Snap camera to pixel grid", ""}, pixelSnap_);
        if (pixelSnap_)
            builder.Float(AuthoringFieldDescriptor{"pixelsPerUnit", "Pixels per world unit", ""}, pixelsPerUnit_);
    }
    builder.Float(AuthoringFieldDescriptor{"nearClip", "Near Clip", "Near clipping distance in world units."},
                  nearClip_);
    builder.Float(AuthoringFieldDescriptor{"farClip", "Far Clip", "Far clipping distance in world units."}, farClip_);
    if (builder.Section("Depth of Field", false)) {
        builder.Bool(
            AuthoringFieldDescriptor{"depthOfField.enabled", "Enabled", "Apply depth of field for this camera."},
            depthOfFieldSettings_.enabled);
        if (depthOfFieldSettings_.enabled) {
            const float maxValue = std::numeric_limits<float>::max();
            builder.FloatRange(AuthoringFieldDescriptor{"depthOfField.focusDistance", "Focus Distance",
                                                        "Camera forward-axis view-Z distance in world units."},
                               depthOfFieldSettings_.focusDistance, CAMERA::kMinDepthOfFieldFocusDistance, maxValue,
                               0.05f);
            builder.FloatRange(
                AuthoringFieldDescriptor{"depthOfField.focusRange", "Focus Range",
                                         "Total clear-band width centered on Focus Distance, in world units."},
                depthOfFieldSettings_.focusRange, 0.0f, maxValue, 0.05f);
            builder.FloatRange(AuthoringFieldDescriptor{"depthOfField.nearTransition", "Near Transition",
                                                        "Near-side transition length along view-Z, in world units."},
                               depthOfFieldSettings_.nearTransition, CAMERA::kMinDepthOfFieldTransition, maxValue,
                               0.05f);
            builder.FloatRange(AuthoringFieldDescriptor{"depthOfField.farTransition", "Far Transition",
                                                        "Far-side transition length along view-Z, in world units."},
                               depthOfFieldSettings_.farTransition, CAMERA::kMinDepthOfFieldTransition, maxValue,
                               0.05f);
            builder.FloatRange(AuthoringFieldDescriptor{"depthOfField.nearBlurRadius1080pPixels",
                                                        "Near Blur Radius (1080p pixels)",
                                                        "Radius at 1080p output height; scaled with output height."},
                               depthOfFieldSettings_.nearBlurRadius1080pPixels, 0.0f,
                               CAMERA::kMaxDepthOfFieldBlurRadius1080pPixels, 0.05f);
            builder.FloatRange(AuthoringFieldDescriptor{"depthOfField.farBlurRadius1080pPixels",
                                                        "Far Blur Radius (1080p pixels)",
                                                        "Radius at 1080p output height; scaled with output height."},
                               depthOfFieldSettings_.farBlurRadius1080pPixels, 0.0f,
                               CAMERA::kMaxDepthOfFieldBlurRadius1080pPixels, 0.05f);
        }
    }
    if (builder.Section("Motion Blur", false)) {
        builder.Bool(AuthoringFieldDescriptor{"motionBlur.enabled", "Enabled",
                                              "Blur moving objects and camera motion (needs TAA, DLAA or DLSS)."},
                     motionBlurSettings_.enabled);
        if (motionBlurSettings_.enabled) {
            builder.FloatRange(AuthoringFieldDescriptor{"motionBlur.shutterAngleDegrees", "Shutter Angle",
                                                        "Degrees of the frame the shutter stays open; 180 is film."},
                               motionBlurSettings_.shutterAngleDegrees, 0.0f, 360.0f, 1.0f);
            builder.FloatRange(AuthoringFieldDescriptor{"motionBlur.maxBlur1080pPixels", "Max Blur (1080p pixels)",
                                                        "Longest streak at 1080p output height."},
                               motionBlurSettings_.maxBlur1080pPixels, 0.0f, CAMERA::kMaxMotionBlurBlur1080pPixels,
                               0.5f);
        }
    }
    ClampLens();
}

void CameraActivationVolumeComponent::BuildInspector(IInspectorBuilder& builder) {
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

void CameraFollowComponent::BuildInspector(IInspectorBuilder& builder) {
    bool changed = false;
    changed |= builder.AssetIdPicker(
        AuthoringFieldDescriptor{"cameraProfile", "Camera Profile", "Reusable gameplay camera profile"},
        AssetType::GameplayCamera, gameplayBinding_.profile.value);
    changed |=
        builder.String(AuthoringFieldDescriptor{"cameraMode", "Initial Camera Mode", ""}, gameplayBinding_.initialMode);
    changed |= builder.SceneObjectIdPicker(AuthoringFieldDescriptor{"cameraLookTarget", "Camera Look Target", ""},
                                           gameplayBinding_.lookTarget);
    changed |= builder.Bool(AuthoringFieldDescriptor{"enabled", "Enabled", ""}, enabled_);
    changed |= builder.Int(AuthoringFieldDescriptor{"priority", "Rig Priority", ""}, priority_);
    changed |=
        builder.SceneObjectIdPicker(AuthoringFieldDescriptor{"targetObjectId", "Follow Target", ""}, targetObjectId_);
    changed |= builder.Bool(AuthoringFieldDescriptor{"useOwnerAsFallbackTarget", "Use Parent as Target Fallback", ""},
                            useOwnerAsFallbackTarget_);

    changed |= builder.Float(AuthoringFieldDescriptor{"pivotOffset.x", "Pivot Offset X", ""}, settings_.pivotOffset.x);
    changed |= builder.Float(AuthoringFieldDescriptor{"pivotOffset.y", "Pivot Offset Y", ""}, settings_.pivotOffset.y);
    changed |= builder.Float(AuthoringFieldDescriptor{"pivotOffset.z", "Pivot Offset Z", ""}, settings_.pivotOffset.z);
    changed |=
        builder.Float(AuthoringFieldDescriptor{"initialYawDegrees", "Initial Yaw", ""}, settings_.initialYawDegrees);
    changed |= builder.Float(AuthoringFieldDescriptor{"initialPitchDegrees", "Initial Pitch", ""},
                             settings_.initialPitchDegrees);
    changed |=
        builder.Float(AuthoringFieldDescriptor{"initialDistance", "Initial Distance", ""}, settings_.initialDistance);
    changed |= builder.Float(AuthoringFieldDescriptor{"minimumPitchDegrees", "Minimum Pitch", ""},
                             settings_.minimumPitchDegrees);
    changed |= builder.Float(AuthoringFieldDescriptor{"maximumPitchDegrees", "Maximum Pitch", ""},
                             settings_.maximumPitchDegrees);
    changed |=
        builder.Float(AuthoringFieldDescriptor{"minimumDistance", "Minimum Distance", ""}, settings_.minimumDistance);
    changed |=
        builder.Float(AuthoringFieldDescriptor{"maximumDistance", "Maximum Distance", ""}, settings_.maximumDistance);
    changed |= builder.Float(AuthoringFieldDescriptor{"followSmooth", "Follow Smooth", ""}, settings_.followSmooth);
    changed |= builder.Float(AuthoringFieldDescriptor{"lookSmooth", "Look Smooth", ""}, settings_.lookSmooth);
    changed |=
        builder.Float(AuthoringFieldDescriptor{"distanceSmooth", "Distance Smooth", ""}, settings_.distanceSmooth);

    changed |= builder.Bool(AuthoringFieldDescriptor{"orbitInputEnabled", "Enable Orbit Input", ""},
                            settings_.orbitInputEnabled);
    changed |= builder.InputActionIdPicker(AuthoringFieldDescriptor{"lookActionId", "Look Action", ""},
                                           INPUT::InputActionValueType::Axis2D, settings_.lookActionId);
    changed |= builder.InputActionIdPicker(AuthoringFieldDescriptor{"zoomActionId", "Zoom Action", ""},
                                           INPUT::InputActionValueType::Axis1D, settings_.zoomActionId);
    changed |= builder.InputActionIdPicker(AuthoringFieldDescriptor{"recenterActionId", "Recenter Action", ""},
                                           INPUT::InputActionValueType::Button, settings_.recenterActionId);
    changed |= builder.Float(AuthoringFieldDescriptor{"yawSpeedDegreesPerSecond", "Gamepad Yaw Speed (deg/s)", ""},
                             settings_.yawSpeedDegreesPerSecond);
    changed |= builder.Float(AuthoringFieldDescriptor{"pitchSpeedDegreesPerSecond", "Gamepad Pitch Speed (deg/s)", ""},
                             settings_.pitchSpeedDegreesPerSecond);
    changed |=
        builder.Float(AuthoringFieldDescriptor{"mouseSensitivityDegreesPerPixel", "Mouse Sensitivity (deg/pixel)", ""},
                      settings_.mouseSensitivityDegreesPerPixel);
    changed |= builder.Float(AuthoringFieldDescriptor{"zoomSpeedUnitsPerSecond", "Gamepad Zoom Speed (units/s)", ""},
                             settings_.zoomSpeedUnitsPerSecond);
    changed |= builder.Float(AuthoringFieldDescriptor{"mouseWheelZoomUnitsPerStep", "Mouse Wheel Zoom Step", ""},
                             settings_.mouseWheelZoomUnitsPerStep);
    changed |= builder.Bool(AuthoringFieldDescriptor{"invertVerticalLook", "Invert Vertical Look", ""},
                            settings_.invertVerticalLook);
    changed |= builder.Bool(AuthoringFieldDescriptor{"autoRecenterEnabled", "Auto Recenter", ""},
                            settings_.autoRecenterEnabled);
    changed |= builder.Float(AuthoringFieldDescriptor{"autoRecenterDelaySeconds", "Recenter Delay", ""},
                             settings_.autoRecenterDelaySeconds);
    changed |=
        builder.Float(AuthoringFieldDescriptor{"autoRecenterSpeedDegreesPerSecond", "Recenter Speed (deg/s)", ""},
                      settings_.autoRecenterSpeedDegreesPerSecond);

    changed |=
        builder.Bool(AuthoringFieldDescriptor{"collisionEnabled", "Camera Collision", ""}, settings_.collisionEnabled);
    changed |= builder.Bool(AuthoringFieldDescriptor{"collisionStaticOnly", "Camera Collision Static Only", ""},
                            settings_.collisionStaticOnly);
    changed |=
        builder.Float(AuthoringFieldDescriptor{"collisionRadius", "Collision Radius", ""}, settings_.collisionRadius);
    changed |= builder.Float(AuthoringFieldDescriptor{"collisionPadding", "Collision Padding", ""},
                             settings_.collisionPadding);
    int collisionMask = static_cast<int>(settings_.collisionLayerMask);
    if (builder.Int(AuthoringFieldDescriptor{"collisionLayerMask", "Collision Layer Mask", ""}, collisionMask)) {
        settings_.collisionLayerMask = static_cast<uint32_t>(collisionMask);
        changed = true;
    }

    changed |= builder.Bool(AuthoringFieldDescriptor{"screenCompositionEnabled", "Screen-space Target Composition", ""},
                            settings_.screenCompositionEnabled);
    changed |= builder.Float(AuthoringFieldDescriptor{"screenCompositionPrimaryAnchorX", "Primary Anchor X", ""},
                             settings_.screenCompositionPrimaryAnchorX);
    changed |= builder.Float(AuthoringFieldDescriptor{"screenCompositionPrimaryAnchorY", "Primary Anchor Y", ""},
                             settings_.screenCompositionPrimaryAnchorY);
    changed |= builder.Float(AuthoringFieldDescriptor{"screenCompositionSecondaryAnchorX", "Secondary Anchor X", ""},
                             settings_.screenCompositionSecondaryAnchorX);
    changed |= builder.Float(AuthoringFieldDescriptor{"screenCompositionSecondaryAnchorY", "Secondary Anchor Y", ""},
                             settings_.screenCompositionSecondaryAnchorY);
    changed |= builder.Float(AuthoringFieldDescriptor{"screenCompositionSoftZoneHalfWidth", "Soft Zone Half Width", ""},
                             settings_.screenCompositionSoftZoneHalfWidth);
    changed |=
        builder.Float(AuthoringFieldDescriptor{"screenCompositionSoftZoneHalfHeight", "Soft Zone Half Height", ""},
                      settings_.screenCompositionSoftZoneHalfHeight);
    changed |= builder.Float(AuthoringFieldDescriptor{"screenCompositionHardZoneHalfWidth", "Hard Zone Half Width", ""},
                             settings_.screenCompositionHardZoneHalfWidth);
    changed |=
        builder.Float(AuthoringFieldDescriptor{"screenCompositionHardZoneHalfHeight", "Hard Zone Half Height", ""},
                      settings_.screenCompositionHardZoneHalfHeight);
    changed |= builder.Float(
        AuthoringFieldDescriptor{"screenCompositionPreferredShoulderAngleDegrees", "Preferred Shoulder Angle", ""},
        settings_.screenCompositionPreferredShoulderAngleDegrees);
    changed |= builder.Float(
        AuthoringFieldDescriptor{"screenCompositionMaximumShoulderAngleDegrees", "Maximum Shoulder Angle", ""},
        settings_.screenCompositionMaximumShoulderAngleDegrees);
    changed |=
        builder.Float(AuthoringFieldDescriptor{"screenCompositionFocusHeightBias", "Composition Focus Height Bias", ""},
                      settings_.screenCompositionFocusHeightBias);
    changed |=
        builder.Float(AuthoringFieldDescriptor{"screenCompositionHardResponseScale", "Hard-zone Response Scale", ""},
                      settings_.screenCompositionHardResponseScale);
    changed |= builder.Float(
        AuthoringFieldDescriptor{"screenCompositionCollisionCenteringStrength", "Collision Centering Strength", ""},
        settings_.screenCompositionCollisionCenteringStrength);

    if (runtime_.screenCompositionActive) {
        builder.Text("Runtime composition P(" + std::to_string(runtime_.screenCompositionPrimaryX) + ", " +
                     std::to_string(runtime_.screenCompositionPrimaryY) + ") T(" +
                     std::to_string(runtime_.screenCompositionSecondaryX) + ", " +
                     std::to_string(runtime_.screenCompositionSecondaryY) + ")");
        builder.Text(std::string("Runtime zone: ") +
                     (runtime_.screenCompositionHardViolation
                          ? "hard correction"
                          : (runtime_.screenCompositionSoftViolation ? "soft correction" : "stable")) +
                     " | collision blend=" + std::to_string(runtime_.screenCompositionCollisionBlend));
    }

    if (changed) {
        ClampSettings();
        ResetRuntimeState();
    }
}

} // namespace HIKARI

#endif // defined(HIKARI_WITH_EDITOR)
