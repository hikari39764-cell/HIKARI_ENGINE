#include "HIKARI_CameraFollowSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

#include "HIKARI/Project/FrameContext.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Gameplay/Motion/HIKARI_MotionIntentService.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Scene/Camera/HIKARI_CameraRigService.h"
#include "Scene/Camera/HIKARI_CameraFollowMath.h"
#include "Scene/Camera/HIKARI_CameraCollision.h"
#include "Scene/Camera/HIKARI_CameraScreenComposition.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

namespace {
using namespace CAMERA::FOLLOW;
constexpr CAMERA::CameraRigSourceId kFollowRigSource = CAMERA::MakeCameraRigSourceId("HIKARI.CameraRig.OrbitFollow");
constexpr float kInputEpsilon = 1.0e-4f;
constexpr float kViewEpsilon = 1.0e-5f;
constexpr float kBackwardMovementThreshold = -0.1f;
constexpr float kDefaultGameplayAspect = 16.0f / 9.0f;
constexpr float kMinimumGameplayAspect = 0.01f;
constexpr float kCompositionTargetWeightFadeStart = 0.30f;
constexpr float kCompositionTargetWeightFadeEnd = 0.48f;
constexpr float kCompositionTargetPredictionSeconds = 0.08f;
constexpr float kCompositionMaximumPredictionDistance = 0.65f;

float ScreenCompositionPolicyWeight(float targetWeight) noexcept {
    const float normalized = std::clamp((targetWeight - kCompositionTargetWeightFadeStart) /
                                            (kCompositionTargetWeightFadeEnd - kCompositionTargetWeightFadeStart),
                                        0.0f, 1.0f);
    return normalized * normalized * (3.0f - 2.0f * normalized);
}

MATH::Vec3 ExtractAxis(const MATH::Mat4& matrix, int column) noexcept {
    return {matrix.m[column][0], matrix.m[column][1], matrix.m[column][2]};
}

MATH::Vec3 TransformPoint(const MATH::Mat4& matrix, const MATH::Vec3& point) noexcept {
    const MATH::Vec4 transformed = matrix.TransformPoint({point.x, point.y, point.z, 1.0f});
    return {transformed.x, transformed.y, transformed.z};
}

float ResolveGameplayAspect(const GameplayCameraService* cameraService) noexcept {
    if (cameraService != nullptr && cameraService->camera != nullptr) {
        const float aspect = cameraService->camera->GetAspect();
        if (std::isfinite(aspect) && aspect >= kMinimumGameplayAspect) {
            return aspect;
        }
    }
    return kDefaultGameplayAspect;
}

float ResolveGameplayVerticalFov(const GameplayCameraService* cameraService,
                                 float fallbackVerticalFovRadians) noexcept {
    if (cameraService != nullptr && cameraService->camera != nullptr) {
        const float verticalFovRadians = cameraService->camera->GetFovYRad();
        if (std::isfinite(verticalFovRadians) && verticalFovRadians >= DegreesToRadians(1.0f) &&
            verticalFovRadians <= DegreesToRadians(179.0f)) {
            return verticalFovRadians;
        }
    }
    return fallbackVerticalFovRadians;
}

CAMERA::CameraTargetFramingSubmission ResolveFramingSettings(
    const CameraFollowComponent::RuntimeState& previous,
    const CAMERA::CameraTargetFramingSubmission* current) noexcept {
    CAMERA::CameraTargetFramingSubmission result =
        current != nullptr ? *current : CAMERA::CameraTargetFramingSubmission{};
    if (current == nullptr) {
        result.targetObject = previous.framingTargetObject;
        result.sourceId = previous.framingSourceId;
        result.priority = previous.framingPriority;
        result.dynamics = previous.framingDynamics;
        result.targetWeight = previous.framingTargetWeight;
        result.distanceScale = previous.framingDistanceScale;
        result.maximumDistanceOffset = previous.framingMaximumDistanceOffset;
        result.yawTrackingSpeedDegreesPerSecond = previous.framingYawTrackingSpeed;
        result.preferredPitchDegrees = previous.framingPreferredPitchDegrees;
        result.pitchTrackingSpeedDegreesPerSecond = previous.framingPitchTrackingSpeed;
        result.suppressOrbitInput = previous.framingSuppressOrbitInput;
        result.suppressZoomInput = previous.framingSuppressZoomInput;
    } else {
        result.targetWeight = std::clamp(result.targetWeight, 0.0f, 1.0f);
        result.distanceScale = (std::max)(result.distanceScale, 0.0f);
        result.maximumDistanceOffset = (std::max)(result.maximumDistanceOffset, 0.0f);
        result.yawTrackingSpeedDegreesPerSecond = (std::max)(result.yawTrackingSpeedDegreesPerSecond, 0.0f);
        result.pitchTrackingSpeedDegreesPerSecond = (std::max)(result.pitchTrackingSpeedDegreesPerSecond, 0.0f);
    }
    // Relationship が終了しても、減衰中は直前の構図設定を保持する。
    if (current == nullptr || !current->relationshipShot) {
        result.relationshipPreferredSide = previous.relationshipPreferredSide;
        result.relationshipShoulderAngleDegrees = previous.relationshipShoulderAngleDegrees;
        result.relationshipVerticalFovOffsetRadians = previous.relationshipVerticalFovOffsetRadians;
        result.relationshipRollRadians = previous.relationshipRollRadians;
        result.relationshipSideClearanceBias = previous.relationshipSideClearanceBias;
        result.relationshipDistanceOffset = previous.relationshipDistanceOffset;
        result.relationshipPivotHeightOffset = previous.relationshipPivotHeightOffset;
        result.relationshipFocusLateralOffset = previous.relationshipFocusLateralOffset;
        result.relationshipFocusHeightOffset = previous.relationshipFocusHeightOffset;
        result.relationshipSuppressModifiers = previous.relationshipSuppressModifiers;
    }
    return result;
}

float SmoothFramingWeight(float previous, float desired, float& velocity,
                          const CAMERA::CameraTargetFramingDynamics& dynamics, float dt) noexcept {
    const float smoothSeconds =
        desired > previous + kInputEpsilon ? dynamics.enterSmoothSeconds : dynamics.exitSmoothSeconds;
    return std::clamp(SmoothDampScalar(previous, desired, velocity, smoothSeconds, -1.0f, dt), 0.0f, 1.0f);
}

GameObject* ResolveTargetObject(World& world, GameObject& cameraObject,
                                const CameraFollowComponent& component) noexcept {
    if (GameObject* target = world.FindObject(component.GetTargetObjectId())) {
        return target;
    }
    if (!component.GetUseOwnerAsFallbackTarget()) {
        return nullptr;
    }
    return cameraObject.GetParent();
}

MATH::Mat4 ResolveTargetWorldMatrix(const GameObject& target,
                                    const PresentationTransformService* presentationTransforms) noexcept {
    MATH::Mat4 result = target.GetTransform().GetWorldMatrix();
    if (presentationTransforms != nullptr) {
        (void)presentationTransforms->TryGetWorldMatrix(target.GetRuntimeHandle(), result);
    }
    return result;
}

float ResolveTargetYawDegrees(const MATH::Mat4& targetWorld) noexcept {
    MATH::Vec3 forward = MATH::Normalize(ExtractAxis(targetWorld, 2));
    if (MATH::Length(forward) <= kViewEpsilon) {
        return 0.0f;
    }
    forward.y = 0.0f;
    forward = MATH::Normalize(forward);
    if (MATH::Length(forward) <= kViewEpsilon) {
        return 0.0f;
    }
    // Orbit yaw describes target-to-camera direction, which is the
    // opposite of the target's viewed forward direction.
    return -RadiansToDegrees(std::atan2(forward.x, forward.z));
}

} // namespace

void CameraFollowSystem::OnWorldAttached(World& world) {
    rigService_ = world.Services().Find<CAMERA::CameraRigService>();
    motionIntentService_ = world.Services().Find<GAMEPLAY::MotionIntentService>();
    inputService_ = world.Services().Find<INPUT::InputService>();
    physicsService_ = world.Services().Find<PHYSICS::PhysicsWorldService>();
    presentationTransforms_ = world.Services().Find<PresentationTransformService>();
    runtimePlayState_ = world.Services().Find<RuntimePlayStateService>();
    gameplayCameraService_ = world.Services().Find<GameplayCameraService>();
}

void CameraFollowSystem::OnWorldDetached(World&) {
    rigService_ = nullptr;
    motionIntentService_ = nullptr;
    inputService_ = nullptr;
    physicsService_ = nullptr;
    presentationTransforms_ = nullptr;
    runtimePlayState_ = nullptr;
    gameplayCameraService_ = nullptr;
}

void CameraFollowSystem::LateUpdate(World& world, const FrameContext& frame) {
    if (rigService_ == nullptr) {
        return;
    }
#if defined(HIKARI_WITH_EDITOR)
    if (runtimePlayState_ == nullptr || !runtimePlayState_->IsActive()) {
        return;
    }
#endif

    const float dt = std::isfinite(frame.gameDt) ? (std::max)(frame.gameDt, 0.0f) : 0.0f;
    // Camera input and target framing are presentation behavior.  Keep
    // them responsive while gameplay is being time-dilated; ordinary
    // world-follow smoothing below intentionally retains gameDt.
    const float cameraDt = std::isfinite(frame.unscaledDt) ? std::clamp(frame.unscaledDt, 0.0f, 0.25f) : 0.0f;
    const INPUT::InputSnapshot* input = inputService_ != nullptr ? &inputService_->GetSnapshot() : nullptr;

    world.ForEachObjectWith<CameraComponent, CameraFollowComponent>(
        WorldComponentQueryAnchor::FirstRequiredType,
        [this, input, dt, cameraDt, frameIndex = frame.frameIndex](
            GameObject& cameraObject, CameraComponent& cameraComponent, CameraFollowComponent& component) {
            EvaluateCamera(cameraObject, cameraComponent, component, input, dt, cameraDt, frameIndex);
        });
}

void CameraFollowSystem::EvaluateCamera(GameObject& cameraObject, CameraComponent& cameraComponent,
                                        CameraFollowComponent& component, const INPUT::InputSnapshot* input, float dt,
                                        float cameraDt, uint64_t frameIndex) {
    if (!component.IsEnabled()) {
        component.ResetRuntimeState();
        return;
    }

    const bool hasProfile = CAMERA::PrepareGameplayCamera(cameraObject, component, input, cameraDt);
    GameObject* target = ResolveTargetObject(*cameraObject.GetWorld(), cameraObject, component);
    if (target == nullptr || cameraObject.GetDocumentId().value == 0u) {
        component.ResetRuntimeState();
        return;
    }

    const MATH::Mat4 targetWorld = ResolveTargetWorldMatrix(*target, presentationTransforms_);
    const MATH::Vec3 pivot = TransformPoint(targetWorld, component.Settings().pivotOffset);
    if (!MATH::IsFinite(pivot)) {
        component.ResetRuntimeState();
        return;
    }
    if (!component.Runtime().initialized) {
        const MATH::Vec3 direction =
            OrbitDirection(component.Settings().initialYawDegrees, component.GetInitialPitchDegrees());
        component.InitializeRuntimeState(pivot + direction * component.GetInitialDistance(), pivot);
    }

    if (hasProfile && component.GameplayState().mode.trackLookTarget) {
        if (auto* lookTarget = cameraObject.GetWorld()->FindObject(component.GameplayBinding().lookTarget)) {
            CAMERA::CameraTargetFramingSubmission request;
            request.cameraObjectId = cameraObject.GetDocumentId();
            request.sourceId = CAMERA::MakeCameraRigSourceId("HIKARI.Camera.ProfileLock");
            request.targetObject = lookTarget->GetRuntimeHandle();
            request.priority = component.GetPriority();
            rigService_->SubmitTargetFraming(request);
        }
    }
    CAMERA::CameraTargetFramingSubmission targetFraming{};
    const bool hasFramingSubmission = rigService_->TryResolveTargetFraming(cameraObject.GetDocumentId(), targetFraming);
    GameObject* secondaryTarget = nullptr;
    MATH::Vec3 requestedSecondaryPivot{};
    if (hasFramingSubmission) {
        secondaryTarget = cameraObject.GetWorld()->FindObject(targetFraming.targetObject);
        if (secondaryTarget != nullptr) {
            const MATH::Mat4 secondaryWorld = ResolveTargetWorldMatrix(*secondaryTarget, presentationTransforms_);
            requestedSecondaryPivot = TransformPoint(secondaryWorld, targetFraming.targetPivotOffset);
            if (!MATH::IsFinite(requestedSecondaryPivot)) {
                secondaryTarget = nullptr;
            }
        }
    }

    const bool hadFramingState = component.Runtime().framingInitialized;
    const bool hadFramingSubmission = component.Runtime().framingHasSubmission;
    const RuntimeObjectHandle previousFramingTarget = component.Runtime().framingTargetObject;
    const auto framing = ResolveFramingSettings(component.Runtime(), hasFramingSubmission ? &targetFraming : nullptr);
    const bool relationshipShotSubmission = hasFramingSubmission && targetFraming.relationshipShot;
    const bool hadRelationshipShotState = component.Runtime().relationshipShotInitialized;
    float relationshipYawOffsetDegrees =
        hadRelationshipShotState ? component.Runtime().relationshipYawOffsetDegrees : 0.0f;
    const bool targetHandleChanged =
        hasFramingSubmission && (!hadFramingSubmission || framing.targetObject != previousFramingTarget ||
                                 framing.sourceId != component.Runtime().framingSourceId ||
                                 framing.priority != component.Runtime().framingPriority);
    const float previousFramingWeight = hadFramingState ? component.Runtime().framingWeight : 0.0f;
    const bool framingStateDormant =
        !hadFramingState || (previousFramingWeight <= kInputEpsilon &&
                             std::abs(component.Runtime().framingWeightVelocity) <= kViewEpsilon &&
                             MATH::Length(component.Runtime().framingFocusOffset) <= kViewEpsilon &&
                             MATH::Length(component.Runtime().framingFocusVelocity) <= kViewEpsilon &&
                             MATH::Length(component.Runtime().framingSecondaryPivotVelocity) <= kViewEpsilon &&
                             std::abs(component.Runtime().framingYawVelocity) <= kViewEpsilon &&
                             std::abs(component.Runtime().framingPitchVelocity) <= kViewEpsilon &&
                             std::abs(component.Runtime().framingDistanceVelocity) <= kViewEpsilon);
    bool framingSwitchActive = hadFramingState && component.Runtime().framingSwitchActive;
    float framingSwitchRemainingSeconds =
        hadFramingState ? (std::max)(component.Runtime().framingSwitchRemainingSeconds, 0.0f) : 0.0f;
    if (framingStateDormant) {
        framingSwitchActive = false;
        framingSwitchRemainingSeconds = 0.0f;
    } else if (targetHandleChanged) {
        const float switchSmoothSeconds = (std::max)(framing.dynamics.targetSwitchSmoothSeconds, 0.0f);
        // Let the critically damped switch spring run for a few
        // time constants, then return stable targets to the
        // single focus spring even when the target keeps moving.
        framingSwitchRemainingSeconds =
            std::isfinite(switchSmoothSeconds) ? std::min(switchSmoothSeconds, 10.0f) * 3.0f : 0.0f;
        framingSwitchActive = framingSwitchRemainingSeconds > kInputEpsilon;
    }

    const MATH::Vec3 previousSecondaryPivot = component.Runtime().framingSecondaryPivot;
    MATH::Vec3 secondaryPivot = previousSecondaryPivot;
    MATH::Vec3 secondaryPivotVelocity = component.Runtime().framingSecondaryPivotVelocity;
    if (framingStateDormant) {
        secondaryPivot = secondaryTarget != nullptr ? requestedSecondaryPivot : pivot;
        secondaryPivotVelocity = {};
    } else if (secondaryTarget != nullptr && framingSwitchActive) {
        secondaryPivot = SmoothDampVector(secondaryPivot, requestedSecondaryPivot, secondaryPivotVelocity,
                                          framing.dynamics.targetSwitchSmoothSeconds, cameraDt);
        if (MATH::Length(requestedSecondaryPivot - secondaryPivot) <= kViewEpsilon &&
            MATH::Length(secondaryPivotVelocity) <= kViewEpsilon) {
            framingSwitchActive = false;
        }
    } else if (secondaryTarget != nullptr) {
        // A stable target's presentation pivot is fed directly to
        // the explicit focus spring below.  Only a real handle or
        // source switch needs a secondary-pivot spring.
        secondaryPivot = requestedSecondaryPivot;
        secondaryPivotVelocity = {};
    } else {
        // Keep the last valid pivot while the target is absent;
        // the framing weight and focus then fade out smoothly.
        secondaryPivot = SmoothDampVector(secondaryPivot, secondaryPivot, secondaryPivotVelocity,
                                          framing.dynamics.exitSmoothSeconds, cameraDt);
        if (MATH::Length(secondaryPivotVelocity) <= kViewEpsilon) {
            framingSwitchActive = false;
        }
    }
    if (framingSwitchActive) {
        framingSwitchRemainingSeconds = (std::max)(framingSwitchRemainingSeconds - cameraDt, 0.0f);
        if (framingSwitchRemainingSeconds <= kInputEpsilon) {
            framingSwitchActive = false;
        }
    } else {
        framingSwitchRemainingSeconds = 0.0f;
    }

    float framingWeightVelocity = hadFramingState ? component.Runtime().framingWeightVelocity : 0.0f;
    const float desiredFramingWeight =
        secondaryTarget != nullptr && hasFramingSubmission ? std::clamp(targetFraming.weight, 0.0f, 1.0f) : 0.0f;
    const float clampedFramingWeight = SmoothFramingWeight(previousFramingWeight, desiredFramingWeight,
                                                           framingWeightVelocity, framing.dynamics, cameraDt);
    const float previousRelationshipWeight = hadRelationshipShotState ? component.Runtime().relationshipWeight : 0.0f;
    float relationshipWeightVelocity = hadRelationshipShotState ? component.Runtime().relationshipWeightVelocity : 0.0f;
    const float desiredRelationshipWeight =
        relationshipShotSubmission && secondaryTarget != nullptr ? std::clamp(targetFraming.weight, 0.0f, 1.0f) : 0.0f;
    const float clampedRelationshipWeight = SmoothFramingWeight(previousRelationshipWeight, desiredRelationshipWeight,
                                                                relationshipWeightVelocity, framing.dynamics, cameraDt);
    const bool relationshipShotResponseActive =
        desiredRelationshipWeight > kInputEpsilon || clampedRelationshipWeight > kInputEpsilon;
    const bool hasTargetFraming =
        secondaryTarget != nullptr && hasFramingSubmission && desiredFramingWeight > kInputEpsilon;
    const bool framingResponseActive = desiredFramingWeight > kInputEpsilon || clampedFramingWeight > kInputEpsilon ||
                                       MATH::Length(component.Runtime().framingFocusOffset) > kViewEpsilon;

    float yaw = component.Runtime().yawDegrees;
    float pitch = component.Runtime().pitchDegrees;
    float desiredDistance = component.Runtime().desiredDistance;
    if (hasProfile && component.GameplayState().changed) {
        desiredDistance = component.GetInitialDistance();
        const auto& modeState = component.GameplayState();
        if (!modeState.mode.keepUserOrbit) {
            yaw = component.Settings().initialYawDegrees;
            pitch = component.GetInitialPitchDegrees();
        } else if (modeState.hasUserOrbit) {
            yaw = modeState.userYaw;
            pitch = modeState.userPitch;
        }
    }
    float lookIdleSeconds = component.Runtime().lookIdleSeconds;

    std::array<float, 2> look{};
    float zoom = 0.0f;
    bool recenterPressed = false;
    if (input != nullptr && component.Settings().orbitInputEnabled) {
        const float inputRestore = 1.0f - clampedFramingWeight;
        if (!component.Settings().lookActionId.empty()) {
            look = input->GetAxis2D(component.Settings().lookActionId);
            if (framing.suppressOrbitInput) {
                look[0] *= inputRestore;
                look[1] *= inputRestore;
            }
        }
        if (!component.Settings().zoomActionId.empty()) {
            zoom = input->GetAxis1D(component.Settings().zoomActionId);
            if (framing.suppressZoomInput) {
                zoom *= inputRestore;
            }
        }
        if (!std::isfinite(look[0])) {
            look[0] = 0.0f;
        }
        if (!std::isfinite(look[1])) {
            look[1] = 0.0f;
        }
        if (!std::isfinite(zoom)) {
            zoom = 0.0f;
        }
        if (!component.Settings().recenterActionId.empty() && inputRestore > kInputEpsilon) {
            recenterPressed = input->IsPressed(component.Settings().recenterActionId);
        }
    }

    const bool hasLookInput = std::abs(look[0]) > kInputEpsilon || std::abs(look[1]) > kInputEpsilon;
    if (hasLookInput) {
        const bool gamepadLook = input != nullptr && input->GetLastActiveDevice() == INPUT::InputDeviceKind::Gamepad;
        const float yawAmount = gamepadLook ? component.Settings().yawSpeedDegreesPerSecond * cameraDt
                                            : component.Settings().mouseSensitivityDegreesPerPixel;
        const float pitchAmount = gamepadLook ? component.Settings().pitchSpeedDegreesPerSecond * cameraDt
                                              : component.Settings().mouseSensitivityDegreesPerPixel;
        // この軌道の yaw 増加は、右手系ビューの画面右への旋回になる。
        yaw += look[0] * yawAmount;
        const float verticalSign = component.Settings().invertVerticalLook ? -1.0f : 1.0f;
        pitch += look[1] * verticalSign * pitchAmount;
        lookIdleSeconds = 0.0f;
    } else {
        lookIdleSeconds += cameraDt;
    }

    if (std::abs(zoom) > kInputEpsilon) {
        const bool gamepadZoom = input != nullptr && input->GetLastActiveDevice() == INPUT::InputDeviceKind::Gamepad;
        const float zoomAmount = gamepadZoom ? component.Settings().zoomSpeedUnitsPerSecond * cameraDt
                                             : component.Settings().mouseWheelZoomUnitsPerStep;
        desiredDistance -= zoom * zoomAmount;
    }
    desiredDistance =
        std::clamp(desiredDistance, component.Settings().minimumDistance, component.Settings().maximumDistance);

    const float followAmount = SmoothFactor(component.Settings().followSmooth, dt);
    const MATH::Vec3 trackedFollowPivot =
        component.Runtime().followPivot + (pivot - component.Runtime().followPivot) * followAmount;
    MATH::Vec3 followPivot = trackedFollowPivot;
    if (relationshipShotResponseActive) {
        followPivot.y += framing.relationshipPivotHeightOffset * clampedRelationshipWeight;
    }
    float compositionCollisionBlend = hadFramingState ? component.Runtime().screenCompositionCollisionBlend : 0.0f;
    float compositionCollisionBlendVelocity =
        hadFramingState ? component.Runtime().screenCompositionCollisionBlendVelocity : 0.0f;
    float compositionSide = hadFramingState ? component.Runtime().screenCompositionSide : 1.0f;
    const float screenCompositionWeight = clampedFramingWeight * ScreenCompositionPolicyWeight(framing.targetWeight);
    const bool screenCompositionActive = component.Settings().screenCompositionEnabled && secondaryTarget != nullptr &&
                                         framingResponseActive && screenCompositionWeight > kInputEpsilon;

    MATH::Vec3 focusVelocity = hadFramingState ? component.Runtime().framingFocusVelocity : MATH::Vec3{};
    MATH::Vec3 focusOffset = hadFramingState ? component.Runtime().framingFocusOffset : MATH::Vec3{};
    const float focusWeight = std::clamp(framing.targetWeight, 0.0f, 1.0f) * clampedFramingWeight;
    MATH::Vec3 desiredFocusOffset = (secondaryPivot - followPivot) * focusWeight;
    if (relationshipShotResponseActive) {
        MATH::Vec3 relationshipForward = secondaryPivot - followPivot;
        relationshipForward.y = 0.0f;
        if (MATH::IsFinite(relationshipForward) && MATH::Length(relationshipForward) > kViewEpsilon) {
            relationshipForward = MATH::Normalize(relationshipForward);
            const MATH::Vec3 relationshipRight{relationshipForward.z, 0.0f, -relationshipForward.x};
            const float relationshipSide = std::abs(relationshipYawOffsetDegrees) > 0.01f
                                               ? relationshipYawOffsetDegrees >= 0.0f ? 1.0f : -1.0f
                                               : 0.0f;
            desiredFocusOffset =
                desiredFocusOffset + relationshipRight * (framing.relationshipFocusLateralOffset * relationshipSide *
                                                          clampedRelationshipWeight);
        }
        desiredFocusOffset.y += framing.relationshipFocusHeightOffset * clampedRelationshipWeight;
    }
    if (screenCompositionActive) {
        const float collisionHeightReduction =
            1.0f - std::clamp(compositionCollisionBlend *
                                  component.Settings().screenCompositionCollisionCenteringStrength * 0.50f,
                              0.0f, 0.85f);
        desiredFocusOffset.y +=
            component.Settings().screenCompositionFocusHeightBias * screenCompositionWeight * collisionHeightReduction;
    }
    focusOffset =
        SmoothDampVector(focusOffset, desiredFocusOffset, focusVelocity, framing.dynamics.focusSmoothSeconds, cameraDt);
    const bool focusResponseActive =
        framingResponseActive || MATH::Length(focusOffset) > kViewEpsilon || MATH::Length(focusVelocity) > kViewEpsilon;
    const MATH::Vec3 lookAt = focusResponseActive
                                  ? followPivot + focusOffset
                                  : component.Runtime().lookAt + (pivot - component.Runtime().lookAt) *
                                                                     SmoothFactor(component.Settings().lookSmooth, dt);

    float framingDistance = desiredDistance;
    if (secondaryTarget != nullptr) {
        MATH::Vec3 separation = secondaryPivot - pivot;
        separation.y = 0.0f;
        const float separationLength = MATH::Length(separation);
        const float scaledSeparation =
            std::isfinite(separationLength) ? separationLength * framing.distanceScale : framing.maximumDistanceOffset;
        const float distanceOffset =
            std::min(framing.maximumDistanceOffset,
                     std::isfinite(scaledSeparation) ? scaledSeparation : framing.maximumDistanceOffset);
        framingDistance = std::clamp(desiredDistance + distanceOffset * clampedFramingWeight,
                                     component.Settings().minimumDistance, component.Settings().maximumDistance);
    }
    if (relationshipShotResponseActive) {
        framingDistance = std::clamp(framingDistance + framing.relationshipDistanceOffset * clampedRelationshipWeight,
                                     component.Settings().minimumDistance, component.Settings().maximumDistance);
    }

    const float compositionDistance =
        std::clamp(std::isfinite(component.Runtime().distance) ? component.Runtime().distance : framingDistance,
                   component.Settings().minimumDistance, component.Settings().maximumDistance);
    // An active relationship shot owns the pose FOV. Starting
    // from the authored component FOV prevents the previous
    // frame's relationship offset and ordinary modifiers from
    // being accumulated again on every LateUpdate.
    float verticalFovRadians = hasProfile ? DegreesToRadians(component.GameplayState().mode.verticalFovDegrees)
                               : relationshipShotResponseActive
                                   ? cameraComponent.GetFovYRad()
                                   : ResolveGameplayVerticalFov(gameplayCameraService_, cameraComponent.GetFovYRad());
    if (relationshipShotResponseActive) {
        verticalFovRadians =
            std::clamp(verticalFovRadians + framing.relationshipVerticalFovOffsetRadians * clampedRelationshipWeight,
                       DegreesToRadians(5.0f), DegreesToRadians(150.0f));
    }
    const float gameplayAspect = ResolveGameplayAspect(gameplayCameraService_);
    MATH::Vec3 compositionSecondaryPivot = secondaryPivot;
    if (screenCompositionActive && hadFramingSubmission && !targetHandleChanged && !framingSwitchActive &&
        cameraDt > kViewEpsilon) {
        MATH::Vec3 prediction =
            (secondaryPivot - previousSecondaryPivot) * (kCompositionTargetPredictionSeconds / cameraDt);
        const float predictionLength = MATH::Length(prediction);
        if (MATH::IsFinite(prediction) && std::isfinite(predictionLength) &&
            predictionLength > kCompositionMaximumPredictionDistance) {
            prediction = prediction * (kCompositionMaximumPredictionDistance / predictionLength);
        }
        if (MATH::IsFinite(prediction)) {
            compositionSecondaryPivot = compositionSecondaryPivot + prediction;
        }
    }
    MATH::Vec3 targetDirection = secondaryPivot - pivot;
    targetDirection.y = 0.0f;
    const bool hasRelationDirection = MATH::IsFinite(targetDirection) && MATH::Length(targetDirection) > kViewEpsilon;
    float relationYaw = yaw;
    if (hasRelationDirection) {
        targetDirection = MATH::Normalize(targetDirection);
        relationYaw = -RadiansToDegrees(std::atan2(targetDirection.x, targetDirection.z));
    }
    float compositionRelationYaw = relationYaw;
    MATH::Vec3 compositionTargetDirection = compositionSecondaryPivot - pivot;
    compositionTargetDirection.y = 0.0f;
    if (screenCompositionActive && MATH::IsFinite(compositionTargetDirection) &&
        MATH::Length(compositionTargetDirection) > kViewEpsilon) {
        compositionTargetDirection = MATH::Normalize(compositionTargetDirection);
        compositionRelationYaw =
            -RadiansToDegrees(std::atan2(compositionTargetDirection.x, compositionTargetDirection.z));
    }

    const bool chooseRelationshipSide =
        relationshipShotSubmission && hasRelationDirection &&
        (!hadRelationshipShotState || targetHandleChanged || !component.Runtime().relationshipShotHasSubmission);
    if (chooseRelationshipSide) {
        const float shoulderAngle = std::clamp(framing.relationshipShoulderAngleDegrees, 0.0f, 75.0f);
        const float preferredSign = std::abs(framing.relationshipPreferredSide) > 0.10f
                                        ? framing.relationshipPreferredSide >= 0.0f ? 1.0f : -1.0f
                                    : compositionSide >= 0.0f ? 1.0f
                                                              : -1.0f;
        const float preferredOffset = preferredSign * shoulderAngle;
        const float alternateOffset = -preferredOffset;
        const auto clearanceForOffset = [&](float yawOffset) {
            bool limited = false;
            return ResolveCollisionDistance(
                physicsService_, component, *target, followPivot,
                OrbitDirection(WrapDegrees(relationYaw + yawOffset), framing.preferredPitchDegrees), framingDistance,
                limited);
        };
        const float preferredClearance = clearanceForOffset(preferredOffset);
        const float alternateClearance = clearanceForOffset(alternateOffset);
        const float centeredClearance = clearanceForOffset(0.0f);
        const float bestOtherClearance = (std::max)(alternateClearance, centeredClearance);
        if (preferredClearance + framing.relationshipSideClearanceBias >= bestOtherClearance) {
            relationshipYawOffsetDegrees = preferredOffset;
        } else if (alternateClearance >= centeredClearance) {
            relationshipYawOffsetDegrees = alternateOffset;
        } else {
            relationshipYawOffsetDegrees = 0.0f;
        }
        if (std::abs(relationshipYawOffsetDegrees) > 0.01f) {
            compositionSide = relationshipYawOffsetDegrees >= 0.0f ? 1.0f : -1.0f;
        } else {
            compositionSide = preferredSign;
        }
    } else if (relationshipShotSubmission && hasRelationDirection && std::abs(relationshipYawOffsetDegrees) > 0.01f) {
        // Keep the collision-selected side fixed, but allow the
        // authored shoulder magnitude to evolve with the shot.
        relationshipYawOffsetDegrees = (relationshipYawOffsetDegrees >= 0.0f ? 1.0f : -1.0f) *
                                       std::clamp(framing.relationshipShoulderAngleDegrees, 0.0f, 75.0f);
    }

    if (!relationshipShotResponseActive && screenCompositionActive && hasRelationDirection &&
        (framingStateDormant || targetHandleChanged || !component.Runtime().screenCompositionActive)) {
        const MATH::Vec3 currentEye = followPivot + OrbitDirection(yaw, pitch) * compositionDistance;
        const ViewportProjection primaryProjection =
            ProjectPointToViewport(currentEye, lookAt, followPivot, verticalFovRadians, gameplayAspect);
        const ViewportProjection secondaryProjection =
            ProjectPointToViewport(currentEye, lookAt, secondaryPivot, verticalFovRadians, gameplayAspect);
        if (primaryProjection.valid && secondaryProjection.valid &&
            std::abs(secondaryProjection.x - primaryProjection.x) > 0.015f) {
            compositionSide = secondaryProjection.x >= primaryProjection.x ? 1.0f : -1.0f;
        } else {
            const float currentShoulder = WrapDegrees(yaw - relationYaw);
            if (std::abs(currentShoulder) > 3.0f) {
                compositionSide = currentShoulder >= 0.0f ? 1.0f : -1.0f;
            }
        }
    }

    const ScreenCompositionTargets compositionTargets =
        BuildCompositionTargets(component, compositionSide, screenCompositionWeight, compositionCollisionBlend);
    const MATH::Vec3 compositionEye = followPivot + OrbitDirection(yaw, pitch) * compositionDistance;
    const ScreenCompositionEvaluation currentComposition =
        screenCompositionActive
            ? EvaluateScreenComposition(followPivot, compositionSecondaryPivot, compositionEye, lookAt,
                                        verticalFovRadians, gameplayAspect, compositionTargets)
            : ScreenCompositionEvaluation{};

    float framingYawVelocity = hadFramingState ? component.Runtime().framingYawVelocity : 0.0f;
    float framingPitchVelocity = hadFramingState ? component.Runtime().framingPitchVelocity : 0.0f;
    if (framingResponseActive) {
        if (framing.yawTrackingSpeedDegreesPerSecond <= kInputEpsilon) {
            framingYawVelocity = 0.0f;
        } else if (hasRelationDirection) {
            float targetYaw = relationYaw;
            float yawResponseWeight = clampedFramingWeight;
            float yawSmoothSeconds = framing.dynamics.yawSmoothSeconds;
            if (relationshipShotResponseActive) {
                targetYaw = WrapDegrees(relationYaw + relationshipYawOffsetDegrees);
                yawResponseWeight = clampedRelationshipWeight;
            } else if (screenCompositionActive) {
                targetYaw = currentComposition.horizontalSoftViolation
                                ? SearchCompositionYaw(
                                      component, followPivot, compositionSecondaryPivot, lookAt, compositionRelationYaw,
                                      compositionSide, pitch, compositionDistance, verticalFovRadians, gameplayAspect,
                                      screenCompositionWeight, compositionCollisionBlend, compositionTargets)
                                : yaw;
                if (currentComposition.horizontalHardViolation) {
                    yawSmoothSeconds *= component.Settings().screenCompositionHardResponseScale;
                }
            }
            const float yawError = WrapDegrees(targetYaw - yaw);
            const float deadZone = std::clamp(framing.dynamics.yawDeadZoneDegrees, 0.0f, 180.0f);
            const float outsideDeadZone = (std::max)(std::abs(yawError) - deadZone, 0.0f);
            const float yawTarget = yaw + (yawError >= 0.0f ? 1.0f : -1.0f) * outsideDeadZone * yawResponseWeight;
            yaw = SmoothDampAngle(yaw, yawTarget, framingYawVelocity, yawSmoothSeconds,
                                  (std::max)(framing.yawTrackingSpeedDegreesPerSecond, 0.0f) * yawResponseWeight,
                                  cameraDt);
        } else {
            framingYawVelocity = 0.0f;
        }

        const float preferredPitch = std::clamp(framing.preferredPitchDegrees, component.Settings().minimumPitchDegrees,
                                                component.Settings().maximumPitchDegrees);
        float resolvedPitchTarget = preferredPitch;
        float pitchSmoothSeconds = framing.dynamics.pitchSmoothSeconds;
        if (screenCompositionActive) {
            resolvedPitchTarget =
                currentComposition.verticalSoftViolation
                    ? SearchCompositionPitch(component, followPivot, compositionSecondaryPivot, lookAt, yaw,
                                             preferredPitch, compositionDistance, verticalFovRadians, gameplayAspect,
                                             screenCompositionWeight, compositionTargets)
                    : pitch;
            if (currentComposition.verticalHardViolation) {
                pitchSmoothSeconds *= component.Settings().screenCompositionHardResponseScale;
            }
        }
        const float pitchTarget = pitch + (resolvedPitchTarget - pitch) * clampedFramingWeight;
        if (framing.pitchTrackingSpeedDegreesPerSecond <= kInputEpsilon) {
            framingPitchVelocity = 0.0f;
        } else {
            pitch = SmoothDampScalar(pitch, pitchTarget, framingPitchVelocity, pitchSmoothSeconds,
                                     framing.pitchTrackingSpeedDegreesPerSecond * clampedFramingWeight, cameraDt);
        }
        lookIdleSeconds = 0.0f;
    } else {
        framingYawVelocity = 0.0f;
        framingPitchVelocity = 0.0f;
    }

    const float targetSpeed = dt > kViewEpsilon ? MATH::Length(pivot - component.Runtime().targetPivot) / dt : 0.0f;
    GAMEPLAY::MotionIntent motionIntent{};
    const bool movingBackward =
        motionIntentService_ != nullptr &&
        motionIntentService_->PeekIntent(target->GetRuntimeHandle(), frameIndex, motionIntent) &&
        motionIntent.move.y < kBackwardMovementThreshold;
    const bool automaticRecenter = !hasTargetFraming && clampedFramingWeight <= kInputEpsilon &&
                                   component.Settings().autoRecenterEnabled && !movingBackward && targetSpeed > 0.05f &&
                                   lookIdleSeconds >= component.Settings().autoRecenterDelaySeconds;
    if (recenterPressed || automaticRecenter) {
        const float recenterDelta =
            recenterPressed ? 360.0f : component.Settings().autoRecenterSpeedDegreesPerSecond * cameraDt;
        yaw = MoveTowardsAngle(yaw, ResolveTargetYawDegrees(targetWorld), recenterDelta);
    }
    yaw = WrapDegrees(yaw);
    pitch = std::clamp(pitch, component.Settings().minimumPitchDegrees, component.Settings().maximumPitchDegrees);
    const MATH::Vec3 orbitDirection = OrbitDirection(yaw, pitch);

    bool collisionLimited = false;
    const float collisionDistance = ResolveCollisionDistance(physicsService_, component, *target, followPivot,
                                                             orbitDirection, framingDistance, collisionLimited);
    float resolvedDistance = component.Runtime().distance;
    float distanceVelocity = hadFramingState ? component.Runtime().framingDistanceVelocity : 0.0f;
    if (!std::isfinite(resolvedDistance)) {
        resolvedDistance = framingDistance;
        distanceVelocity = 0.0f;
    }
    const bool collisionPushIn = collisionLimited && collisionDistance < resolvedDistance;
    if (collisionPushIn) {
        resolvedDistance = collisionDistance;
        distanceVelocity = 0.0f;
    } else if (framingResponseActive || (component.Runtime().collisionLimited && !collisionLimited)) {
        const bool releasingCollision = component.Runtime().collisionLimited && !collisionLimited;
        const bool expanding = collisionDistance > resolvedDistance;
        const float smoothSeconds = releasingCollision ? framing.dynamics.distanceContractSmoothSeconds
                                                       : (expanding ? framing.dynamics.distanceExpandSmoothSeconds
                                                                    : framing.dynamics.distanceContractSmoothSeconds);
        resolvedDistance =
            SmoothDampScalar(resolvedDistance, collisionDistance, distanceVelocity, smoothSeconds, -1.0f, cameraDt);
    } else {
        resolvedDistance +=
            (collisionDistance - resolvedDistance) * SmoothFactor(component.Settings().distanceSmooth, dt);
        // The ordinary world-follow path owns distance directly;
        // do not retain a stale framing spring velocity here.
        distanceVelocity = 0.0f;
    }

    const MATH::Vec3 eye = followPivot + orbitDirection * resolvedDistance;

    const float collisionRange = (std::max)(framingDistance - component.Settings().minimumDistance, 0.01f);
    const float collisionPressure =
        collisionLimited ? std::clamp((framingDistance - resolvedDistance) / collisionRange * 1.4f, 0.0f, 1.0f) : 0.0f;
    const float collisionBlendSmoothSeconds = collisionPressure > compositionCollisionBlend ? 0.10f : 0.28f;
    compositionCollisionBlend =
        SmoothDampScalar(compositionCollisionBlend, screenCompositionActive ? collisionPressure : 0.0f,
                         compositionCollisionBlendVelocity, collisionBlendSmoothSeconds, -1.0f, cameraDt);
    const ScreenCompositionTargets finalCompositionTargets =
        BuildCompositionTargets(component, compositionSide, screenCompositionWeight, compositionCollisionBlend);
    const ScreenCompositionEvaluation finalComposition =
        screenCompositionActive ? EvaluateScreenComposition(followPivot, secondaryPivot, eye, lookAt,
                                                            verticalFovRadians, gameplayAspect, finalCompositionTargets)
                                : ScreenCompositionEvaluation{};

    component.SetRuntimeOrbit(yaw, pitch, desiredDistance, resolvedDistance, lookIdleSeconds);
    component.SetRuntimeView(eye, lookAt, collisionLimited);
    component.SetRuntimeTracking(trackedFollowPivot, pivot);
    component.SetRuntimeFramingState(
        hasFramingSubmission, framing.targetObject, framing.sourceId, framing.priority, framing.dynamics,
        framing.targetWeight, framing.distanceScale, framing.maximumDistanceOffset,
        framing.yawTrackingSpeedDegreesPerSecond, framing.preferredPitchDegrees,
        framing.pitchTrackingSpeedDegreesPerSecond, framing.suppressOrbitInput, framing.suppressZoomInput,
        framingSwitchActive, framingSwitchRemainingSeconds, secondaryPivot, clampedFramingWeight, framingWeightVelocity,
        secondaryPivotVelocity, framingYawVelocity, framingPitchVelocity, focusOffset, focusVelocity, distanceVelocity);
    component.SetRuntimeRelationshipShotState(
        relationshipShotSubmission, framing.relationshipPreferredSide, framing.relationshipShoulderAngleDegrees,
        framing.relationshipVerticalFovOffsetRadians, framing.relationshipRollRadians,
        framing.relationshipSideClearanceBias, framing.relationshipDistanceOffset,
        framing.relationshipPivotHeightOffset, framing.relationshipFocusLateralOffset,
        framing.relationshipFocusHeightOffset, framing.relationshipSuppressModifiers, relationshipYawOffsetDegrees,
        clampedRelationshipWeight, relationshipWeightVelocity);
    component.SetRuntimeScreenCompositionState(screenCompositionActive, compositionSide,
                                               screenCompositionActive ? finalComposition.primary.x : 0.5f,
                                               screenCompositionActive ? finalComposition.primary.y : 0.5f,
                                               screenCompositionActive ? finalComposition.secondary.x : 0.5f,
                                               screenCompositionActive ? finalComposition.secondary.y : 0.5f,
                                               screenCompositionActive && finalComposition.HasSoftViolation(),
                                               screenCompositionActive && finalComposition.HasHardViolation(),
                                               compositionCollisionBlend, compositionCollisionBlendVelocity);

    CAMERA::CameraRigSubmission submission{};
    submission.cameraObjectId = cameraObject.GetDocumentId();
    submission.sourceId = kFollowRigSource;
    submission.priority = component.GetPriority();
    submission.pose.eye = eye;
    submission.pose.target = lookAt;
    MATH::Vec3 resolvedUp{0.0f, 1.0f, 0.0f};
    if (relationshipShotResponseActive && std::abs(framing.relationshipRollRadians) > kViewEpsilon) {
        const MATH::Vec3 viewForward = MATH::Normalize(lookAt - eye);
        if (MATH::Length(viewForward) > kViewEpsilon) {
            const float roll = framing.relationshipRollRadians * clampedRelationshipWeight;
            const MATH::Vec3 viewRight = MATH::BuildViewBasisRH(viewForward, resolvedUp).right;
            resolvedUp = MATH::Normalize(resolvedUp * std::cos(roll) + viewRight * std::sin(roll));
        }
    }
    submission.pose.up = resolvedUp;
    submission.pose.overrideVerticalFov = relationshipShotResponseActive;
    submission.pose.verticalFovRadians = verticalFovRadians;
    submission.pose.modifierWeightScale = relationshipShotResponseActive && framing.relationshipSuppressModifiers
                                              ? 1.0f - clampedRelationshipWeight
                                              : 1.0f;
    if (hasProfile)
        CAMERA::ApplyGameplayCameraPose(cameraObject, component, pivot, targetSpeed, cameraDt, submission.pose);
    if (rigService_->SubmitPose(submission) &&
        rigService_ == cameraObject.GetWorld()->Services().Find<CAMERA::CameraRigService>()) {
        RuntimeSceneContext::MarkCameraReady(*cameraObject.GetWorld());
    }
}

bool CameraFollowSystem::EvaluatePreview(World& world, GameObject& camera, CameraFollowComponent& preview,
                                         float seconds, const INPUT::InputSnapshot* input) {
    OnWorldAttached(world);
    auto* lens = camera.GetComponent<CameraComponent>();
    if (!lens || !rigService_)
        return false;
    seconds = std::clamp(std::isfinite(seconds) ? seconds : 0.0f, 0.0f, 0.1f);
    // プレビューの姿勢・構図はライブのリグ提出へ書き戻さない。
    auto* liveRig = rigService_;
    auto previewRig = *liveRig;
    rigService_ = &previewRig;
    EvaluateCamera(camera, *lens, preview, input, seconds, seconds, liveRig->GetFrameIndex());
    rigService_ = liveRig;
    return preview.Runtime().initialized;
}
} // namespace HIKARI
