#include "HIKARI_CameraFollowSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "Core/HIKARI_FrameContext.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Gameplay/Motion/HIKARI_MotionIntentService.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Physics/HIKARI_PhysicsTypes.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Scene/Camera/HIKARI_CameraRigService.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/HIKARI_CameraFollowComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    namespace {
        constexpr CAMERA::CameraRigSourceId kFollowRigSource =
            CAMERA::MakeCameraRigSourceId(
                "HIKARI.CameraRig.OrbitFollow");
        constexpr float kInputEpsilon = 1.0e-4f;
        constexpr float kViewEpsilon = 1.0e-5f;
        constexpr float kBackwardMovementThreshold = -0.1f;

        float DegreesToRadians(float degrees) noexcept {
            return degrees * std::numbers::pi_v<float> / 180.0f;
        }

        float RadiansToDegrees(float radians) noexcept {
            return radians * 180.0f / std::numbers::pi_v<float>;
        }

        float SmoothFactor(float smooth, float dt) noexcept {
            if (smooth <= 0.0f || dt <= 0.0f) {
                return 1.0f;
            }
            return std::clamp(
                1.0f - std::exp(-smooth * dt),
                0.0f,
                1.0f);
        }

        float WrapDegrees(float degrees) noexcept {
            float wrapped = std::fmod(degrees + 180.0f, 360.0f);
            if (wrapped < 0.0f) {
                wrapped += 360.0f;
            }
            return wrapped - 180.0f;
        }

        float MoveTowardsAngle(
            float current,
            float target,
            float maxDelta) noexcept {

            const float delta = WrapDegrees(target - current);
            if (std::abs(delta) <= maxDelta) {
                return target;
            }
            return current + std::copysign(maxDelta, delta);
        }

        float SmoothDampScalar(
            float current,
            float target,
            float& currentVelocity,
            float smoothTime,
            float maxSpeed,
            float dt) noexcept {

            if (!std::isfinite(current) || !std::isfinite(target) ||
                !std::isfinite(currentVelocity)) {
                currentVelocity = 0.0f;
                return std::isfinite(current) ? current : 0.0f;
            }
            if (dt <= 0.0f || !std::isfinite(dt)) {
                return current;
            }
            if (!std::isfinite(smoothTime) || smoothTime <= kViewEpsilon) {
                currentVelocity = 0.0f;
                return target;
            }

            smoothTime = (std::max)(smoothTime, kViewEpsilon);
            const float omega = 2.0f / smoothTime;
            const float x = omega * dt;
            const float exp = 1.0f /
                (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
            const float originalTarget = target;
            float change = current - target;
            // A negative maxSpeed means unbounded smoothing.  Zero is a real
            // limit: callers use it to disable a tracking channel.
            if (std::isfinite(maxSpeed) && maxSpeed >= 0.0f) {
                const float maxChange = maxSpeed * smoothTime;
                if (std::isfinite(maxChange)) {
                    change = std::clamp(
                        change,
                        -maxChange,
                        maxChange);
                }
            }
            target = current - change;
            const float temp =
                (currentVelocity + omega * change) * dt;
            currentVelocity =
                (currentVelocity - omega * temp) * exp;
            float output = target + (change + temp) * exp;

            if ((originalTarget - current > 0.0f) ==
                    (output > originalTarget)) {
                output = originalTarget;
                currentVelocity = dt > kViewEpsilon
                    ? (output - originalTarget) / dt
                    : 0.0f;
            }
            if (!std::isfinite(output) ||
                !std::isfinite(currentVelocity)) {
                currentVelocity = 0.0f;
                return originalTarget;
            }
            return output;
        }

        float SmoothDampAngle(
            float current,
            float target,
            float& currentVelocity,
            float smoothTime,
            float maxSpeed,
            float dt) noexcept {

            const float delta = WrapDegrees(target - current);
            return SmoothDampScalar(
                current,
                current + delta,
                currentVelocity,
                smoothTime,
                maxSpeed,
                dt);
        }

        MATH::Vec3 SmoothDampVector(
            const MATH::Vec3& current,
            const MATH::Vec3& target,
            MATH::Vec3& currentVelocity,
            float smoothTime,
            float dt) noexcept {

            MATH::Vec3 result = current;
            result.x = SmoothDampScalar(
                current.x,
                target.x,
                currentVelocity.x,
                smoothTime,
                -1.0f,
                dt);
            result.y = SmoothDampScalar(
                current.y,
                target.y,
                currentVelocity.y,
                smoothTime,
                -1.0f,
                dt);
            result.z = SmoothDampScalar(
                current.z,
                target.z,
                currentVelocity.z,
                smoothTime,
                -1.0f,
                dt);
            return MATH::IsFinite(result) ? result : target;
        }

        MATH::Vec3 ExtractAxis(
            const MATH::Mat4& matrix,
            int column) noexcept {

            return {
                matrix.m[column][0],
                matrix.m[column][1],
                matrix.m[column][2]
            };
        }

        MATH::Vec3 TransformPoint(
            const MATH::Mat4& matrix,
            const MATH::Vec3& point) noexcept {

            const MATH::Vec4 transformed = matrix.TransformPoint({
                point.x,
                point.y,
                point.z,
                1.0f
            });
            return { transformed.x, transformed.y, transformed.z };
        }

        MATH::Vec3 OrbitDirection(
            float yawDegrees,
            float pitchDegrees) noexcept {

            const float yaw = DegreesToRadians(yawDegrees);
            const float pitch = DegreesToRadians(pitchDegrees);
            const float horizontal = std::cos(pitch);
            return MATH::Normalize({
                std::sin(yaw) * horizontal,
                std::sin(pitch),
                -std::cos(yaw) * horizontal
            });
        }

        GameObject* ResolveTargetObject(
            World& world,
            GameObject& cameraObject,
            const CameraFollowComponent& component) noexcept {

            if (GameObject* target = world.FindObject(
                    component.GetTargetObjectId())) {
                return target;
            }
            if (!component.GetUseOwnerAsFallbackTarget()) {
                return nullptr;
            }
            return cameraObject.GetParent();
        }

        MATH::Mat4 ResolveTargetWorldMatrix(
            const GameObject& target,
            const PresentationTransformService* presentationTransforms)
            noexcept {
            MATH::Mat4 result =
                target.GetTransform().GetWorldMatrix();
            if (presentationTransforms != nullptr) {
                (void)presentationTransforms->TryGetWorldMatrix(
                    target.GetRuntimeHandle(),
                    result);
            }
            return result;
        }

        float ResolveTargetYawDegrees(
            const MATH::Mat4& targetWorld) noexcept {
            MATH::Vec3 forward = MATH::Normalize(ExtractAxis(
                targetWorld,
                2));
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

        float ResolveCollisionDistance(
            PHYSICS::PhysicsWorldService* physics,
            const CameraFollowComponent& component,
            const GameObject& target,
            const MATH::Vec3& pivot,
            const MATH::Vec3& direction,
            float desiredDistance,
            bool& outLimited) {

            outLimited = false;
            if (physics == nullptr || !component.IsCollisionEnabled() ||
                desiredDistance <= component.GetMinimumDistance()) {
                return desiredDistance;
            }

            PHYSICS::PhysicsShapeCastQuery query{};
            query.shape.type = PHYSICS::PhysicsShapeType::Sphere;
            query.shape.radius = component.GetCollisionRadius();
            query.startPose.position = pivot;
            query.startPose.rotation = MATH::Quat::Identity();
            query.direction = direction;
            query.maxDistance = desiredDistance;
            query.filter.layerMask = component.GetCollisionLayerMask();
            query.filter.includeTriggers = false;
            query.filter.ignoredObject = target.GetRuntimeHandle();

            PHYSICS::PhysicsHit hit{};
            if (!physics->ShapeCast(query, hit)) {
                return desiredDistance;
            }
            const float limitedDistance = std::clamp(
                hit.distance - component.GetCollisionPadding(),
                component.GetMinimumDistance(),
                desiredDistance);
            outLimited = limitedDistance + kViewEpsilon < desiredDistance;
            return limitedDistance;
        }
    }

    void CameraFollowSystem::OnWorldAttached(World& world) {
        rigService_ = world.Services().Find<CAMERA::CameraRigService>();
        motionIntentService_ = world.Services().Find<
            GAMEPLAY::MotionIntentService>();
        inputService_ = world.Services().Find<INPUT::InputService>();
        physicsService_ = world.Services().Find<
            PHYSICS::PhysicsWorldService>();
        presentationTransforms_ = world.Services().Find<
            PresentationTransformService>();
        runtimePlayState_ = world.Services().Find<
            RuntimePlayStateService>();
    }

    void CameraFollowSystem::OnWorldDetached(World&) {
        rigService_ = nullptr;
        motionIntentService_ = nullptr;
        inputService_ = nullptr;
        physicsService_ = nullptr;
        presentationTransforms_ = nullptr;
        runtimePlayState_ = nullptr;
    }

    void CameraFollowSystem::LateUpdate(
        World& world,
        const FrameContext& frame) {

        if (rigService_ == nullptr) {
            return;
        }
#if defined(HIKARI_WITH_EDITOR)
        if (runtimePlayState_ == nullptr ||
            !runtimePlayState_->IsActive()) {
            return;
        }
#endif

        const float dt = std::isfinite(frame.gameDt)
            ? (std::max)(frame.gameDt, 0.0f)
            : 0.0f;
        // Camera input and target framing are presentation behavior.  Keep
        // them responsive while gameplay is being time-dilated; ordinary
        // world-follow smoothing below intentionally retains gameDt.
        const float cameraDt = std::isfinite(frame.unscaledDt)
            ? std::clamp(frame.unscaledDt, 0.0f, 0.25f)
            : 0.0f;
        const INPUT::InputSnapshot* input = inputService_ != nullptr
            ? &inputService_->GetSnapshot()
            : nullptr;

        world.ForEachObjectWith<CameraComponent, CameraFollowComponent>(
            [this, input, dt, cameraDt, frameIndex = frame.frameIndex](
                GameObject& cameraObject,
                CameraComponent&,
                CameraFollowComponent& component) {
                if (!component.IsEnabled()) {
                    component.ResetRuntimeState();
                    return;
                }

                GameObject* target = ResolveTargetObject(
                    *cameraObject.GetWorld(),
                    cameraObject,
                    component);
                if (target == nullptr ||
                    cameraObject.GetDocumentId().value == 0u) {
                    component.ResetRuntimeState();
                    return;
                }

                const MATH::Mat4 targetWorld =
                    ResolveTargetWorldMatrix(
                        *target,
                        presentationTransforms_);
                const MATH::Vec3 pivot = TransformPoint(
                    targetWorld,
                    component.GetPivotOffset());
                if (!MATH::IsFinite(pivot)) {
                    component.ResetRuntimeState();
                    return;
                }
                if (!component.HasRuntimeState()) {
                    const MATH::Vec3 direction = OrbitDirection(
                        component.GetInitialYawDegrees(),
                        component.GetInitialPitchDegrees());
                    component.InitializeRuntimeState(
                        pivot + direction * component.GetInitialDistance(),
                        pivot);
                }

                CAMERA::CameraTargetFramingSubmission targetFraming{};
                const bool hasFramingSubmission =
                    rigService_->TryResolveTargetFraming(
                        cameraObject.GetDocumentId(),
                        targetFraming);
                GameObject* secondaryTarget = nullptr;
                MATH::Vec3 requestedSecondaryPivot{};
                if (hasFramingSubmission) {
                    secondaryTarget = cameraObject.GetWorld()->FindObject(
                        targetFraming.targetObject);
                    if (secondaryTarget != nullptr) {
                        const MATH::Mat4 secondaryWorld =
                            ResolveTargetWorldMatrix(
                                *secondaryTarget,
                                presentationTransforms_);
                        requestedSecondaryPivot = TransformPoint(
                            secondaryWorld,
                            targetFraming.targetPivotOffset);
                        if (!MATH::IsFinite(requestedSecondaryPivot)) {
                            secondaryTarget = nullptr;
                        }
                    }
                }

                const bool hadFramingState =
                    component.HasRuntimeFramingState();
                const bool hadFramingSubmission =
                    component.HasRuntimeFramingSubmission();
                const RuntimeObjectHandle previousFramingTarget =
                    component.GetRuntimeFramingTargetObject();
                const RuntimeObjectHandle framingTarget =
                    hasFramingSubmission
                    ? targetFraming.targetObject
                    : previousFramingTarget;
                const CAMERA::CameraRigSourceId framingSourceId =
                    hasFramingSubmission
                    ? targetFraming.sourceId
                    : component.GetRuntimeFramingSourceId();
                const int framingPriority = hasFramingSubmission
                    ? targetFraming.priority
                    : component.GetRuntimeFramingPriority();
                const CAMERA::CameraTargetFramingDynamics framingDynamics =
                    hasFramingSubmission
                    ? targetFraming.dynamics
                    : component.GetRuntimeFramingDynamics();
                const float framingTargetWeight = hasFramingSubmission
                    ? std::clamp(targetFraming.targetWeight, 0.0f, 1.0f)
                    : component.GetRuntimeFramingTargetWeight();
                const float framingDistanceScale = hasFramingSubmission
                    ? (std::max)(targetFraming.distanceScale, 0.0f)
                    : component.GetRuntimeFramingDistanceScale();
                const float framingMaximumDistanceOffset =
                    hasFramingSubmission
                    ? (std::max)(targetFraming.maximumDistanceOffset, 0.0f)
                    : component.GetRuntimeFramingMaximumDistanceOffset();
                const float framingYawSpeed = hasFramingSubmission
                    ? (std::max)(
                        targetFraming.yawTrackingSpeedDegreesPerSecond,
                        0.0f)
                    : component.GetRuntimeFramingYawTrackingSpeed();
                const float framingPreferredPitch = hasFramingSubmission
                    ? targetFraming.preferredPitchDegrees
                    : component.GetRuntimeFramingPreferredPitch();
                const float framingPitchTrackingSpeed = hasFramingSubmission
                    ? (std::max)(
                        targetFraming.pitchTrackingSpeedDegreesPerSecond,
                        0.0f)
                    : component.GetRuntimeFramingPitchTrackingSpeed();
                const bool suppressOrbitInput = hasFramingSubmission
                    ? targetFraming.suppressOrbitInput
                    : component.GetRuntimeFramingSuppressOrbitInput();
                const bool suppressZoomInput = hasFramingSubmission
                    ? targetFraming.suppressZoomInput
                    : component.GetRuntimeFramingSuppressZoomInput();
                const bool targetHandleChanged = hasFramingSubmission &&
                    (!hadFramingSubmission ||
                        framingTarget != previousFramingTarget ||
                        framingSourceId !=
                            component.GetRuntimeFramingSourceId() ||
                        framingPriority !=
                            component.GetRuntimeFramingPriority());
                const float previousFramingWeight = hadFramingState
                    ? component.GetRuntimeFramingWeight()
                    : 0.0f;
                const bool framingStateDormant = !hadFramingState ||
                    (previousFramingWeight <= kInputEpsilon &&
                        std::abs(component.GetRuntimeFramingWeightVelocity()) <=
                            kViewEpsilon &&
                        MATH::Length(
                            component.GetRuntimeFramingFocusOffset()) <=
                            kViewEpsilon &&
                        MATH::Length(
                            component.GetRuntimeFramingFocusVelocity()) <=
                            kViewEpsilon &&
                        MATH::Length(
                            component.GetRuntimeFramingSecondaryPivotVelocity()) <=
                            kViewEpsilon &&
                        std::abs(component.GetRuntimeFramingYawVelocity()) <=
                            kViewEpsilon &&
                        std::abs(component.GetRuntimeFramingPitchVelocity()) <=
                            kViewEpsilon &&
                        std::abs(component.GetRuntimeFramingDistanceVelocity()) <=
                            kViewEpsilon);
                bool framingSwitchActive = hadFramingState &&
                    component.GetRuntimeFramingSwitchActive();
                float framingSwitchRemainingSeconds = hadFramingState
                    ? (std::max)(
                        component.GetRuntimeFramingSwitchRemainingSeconds(),
                        0.0f)
                    : 0.0f;
                if (framingStateDormant) {
                    framingSwitchActive = false;
                    framingSwitchRemainingSeconds = 0.0f;
                } else if (targetHandleChanged) {
                    const float switchSmoothSeconds = (std::max)(
                        framingDynamics.targetSwitchSmoothSeconds,
                        0.0f);
                    // Let the critically damped switch spring run for a few
                    // time constants, then return stable targets to the
                    // single focus spring even when the target keeps moving.
                    framingSwitchRemainingSeconds = std::isfinite(
                        switchSmoothSeconds)
                        ? std::min(switchSmoothSeconds, 10.0f) * 3.0f
                        : 0.0f;
                    framingSwitchActive =
                        framingSwitchRemainingSeconds > kInputEpsilon;
                }

                MATH::Vec3 secondaryPivot =
                    component.GetRuntimeFramingSecondaryPivot();
                MATH::Vec3 secondaryPivotVelocity = component
                    .GetRuntimeFramingSecondaryPivotVelocity();
                if (framingStateDormant) {
                    secondaryPivot = secondaryTarget != nullptr
                        ? requestedSecondaryPivot
                        : pivot;
                    secondaryPivotVelocity = {};
                } else if (secondaryTarget != nullptr &&
                    framingSwitchActive) {
                    secondaryPivot = SmoothDampVector(
                        secondaryPivot,
                        requestedSecondaryPivot,
                        secondaryPivotVelocity,
                        framingDynamics.targetSwitchSmoothSeconds,
                        cameraDt);
                    if (MATH::Length(
                            requestedSecondaryPivot - secondaryPivot) <=
                            kViewEpsilon &&
                        MATH::Length(secondaryPivotVelocity) <=
                            kViewEpsilon) {
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
                    secondaryPivot = SmoothDampVector(
                        secondaryPivot,
                        secondaryPivot,
                        secondaryPivotVelocity,
                        framingDynamics.exitSmoothSeconds,
                        cameraDt);
                    if (MATH::Length(secondaryPivotVelocity) <=
                        kViewEpsilon) {
                        framingSwitchActive = false;
                    }
                }
                if (framingSwitchActive) {
                    framingSwitchRemainingSeconds = (std::max)(
                        framingSwitchRemainingSeconds - cameraDt,
                        0.0f);
                    if (framingSwitchRemainingSeconds <= kInputEpsilon) {
                        framingSwitchActive = false;
                    }
                } else {
                    framingSwitchRemainingSeconds = 0.0f;
                }

                const float currentFramingWeight = previousFramingWeight;
                float framingWeightVelocity = hadFramingState
                    ? component.GetRuntimeFramingWeightVelocity()
                    : 0.0f;
                const float desiredFramingWeight =
                    secondaryTarget != nullptr && hasFramingSubmission
                    ? std::clamp(targetFraming.weight, 0.0f, 1.0f)
                    : 0.0f;
                const float framingWeightSmoothSeconds =
                    desiredFramingWeight > currentFramingWeight + kInputEpsilon
                    ? framingDynamics.enterSmoothSeconds
                    : framingDynamics.exitSmoothSeconds;
                const float framingWeight = SmoothDampScalar(
                    currentFramingWeight,
                    desiredFramingWeight,
                    framingWeightVelocity,
                    framingWeightSmoothSeconds,
                    -1.0f,
                    cameraDt);
                const float clampedFramingWeight = std::clamp(
                    framingWeight,
                    0.0f,
                    1.0f);
                const bool hasTargetFraming =
                    secondaryTarget != nullptr &&
                    hasFramingSubmission &&
                    desiredFramingWeight > kInputEpsilon;
                const bool framingResponseActive =
                    desiredFramingWeight > kInputEpsilon ||
                    clampedFramingWeight > kInputEpsilon ||
                    MATH::Length(component.GetRuntimeFramingFocusOffset()) >
                        kViewEpsilon;

                float yaw = component.GetRuntimeYawDegrees();
                float pitch = component.GetRuntimePitchDegrees();
                float desiredDistance =
                    component.GetRuntimeDesiredDistance();
                float lookIdleSeconds =
                    component.GetRuntimeLookIdleSeconds();

                std::array<float, 2> look{};
                float zoom = 0.0f;
                bool recenterPressed = false;
                if (input != nullptr && component.IsOrbitInputEnabled()) {
                    const float inputRestore = 1.0f - clampedFramingWeight;
                    if (!component.GetLookActionId().empty()) {
                        look = input->GetAxis2D(
                            component.GetLookActionId());
                        if (suppressOrbitInput) {
                            look[0] *= inputRestore;
                            look[1] *= inputRestore;
                        }
                    }
                    if (!component.GetZoomActionId().empty()) {
                        zoom = input->GetAxis1D(
                            component.GetZoomActionId());
                        if (suppressZoomInput) {
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
                    if (!component.GetRecenterActionId().empty() &&
                        inputRestore > kInputEpsilon) {
                        recenterPressed = input->IsPressed(
                            component.GetRecenterActionId());
                    }
                }

                const bool hasLookInput =
                    std::abs(look[0]) > kInputEpsilon ||
                    std::abs(look[1]) > kInputEpsilon;
                if (hasLookInput) {
                    const bool gamepadLook = input != nullptr &&
                        input->GetLastActiveDevice() ==
                            INPUT::InputDeviceKind::Gamepad;
                    const float yawAmount = gamepadLook
                        ? component.GetYawSpeedDegreesPerSecond() * cameraDt
                        : component.GetMouseSensitivityDegreesPerPixel();
                    const float pitchAmount = gamepadLook
                        ? component.GetPitchSpeedDegreesPerSecond() * cameraDt
                        : component.GetMouseSensitivityDegreesPerPixel();
                    // Orbit direction points from the target to the camera,
                    // so it turns opposite to the viewed direction.
                    yaw -= look[0] * yawAmount;
                    const float verticalSign =
                        component.GetInvertVerticalLook() ? -1.0f : 1.0f;
                    pitch += look[1] * verticalSign * pitchAmount;
                    lookIdleSeconds = 0.0f;
                } else {
                    lookIdleSeconds += cameraDt;
                }

                float framingYawVelocity = hadFramingState
                    ? component.GetRuntimeFramingYawVelocity()
                    : 0.0f;
                float framingPitchVelocity = hadFramingState
                    ? component.GetRuntimeFramingPitchVelocity()
                    : 0.0f;
                if (framingResponseActive) {
                    MATH::Vec3 targetDirection = secondaryPivot - pivot;
                    targetDirection.y = 0.0f;
                    if (framingYawSpeed <= kInputEpsilon) {
                        framingYawVelocity = 0.0f;
                    } else if (MATH::IsFinite(targetDirection) &&
                        MATH::Length(targetDirection) > kViewEpsilon) {
                        targetDirection = MATH::Normalize(targetDirection);
                        const float targetYaw = -RadiansToDegrees(std::atan2(
                            targetDirection.x,
                            targetDirection.z));
                        const float yawError = WrapDegrees(targetYaw - yaw);
                        const float deadZone = std::clamp(
                            framingDynamics.yawDeadZoneDegrees,
                            0.0f,
                            180.0f);
                        const float outsideDeadZone = (std::max)(
                            std::abs(yawError) - deadZone,
                            0.0f);
                        const float yawTarget = yaw +
                            (yawError >= 0.0f ? 1.0f : -1.0f) *
                            outsideDeadZone * clampedFramingWeight;
                        yaw = SmoothDampAngle(
                            yaw,
                            yawTarget,
                            framingYawVelocity,
                            framingDynamics.yawSmoothSeconds,
                            (std::max)(
                                framingYawSpeed,
                                0.0f) * clampedFramingWeight,
                            cameraDt);
                    } else {
                        framingYawVelocity = 0.0f;
                    }
                    const float preferredPitch = std::clamp(
                        framingPreferredPitch,
                        component.GetMinimumPitchDegrees(),
                        component.GetMaximumPitchDegrees());
                    const float pitchTarget = pitch +
                        (preferredPitch - pitch) * clampedFramingWeight;
                    if (framingPitchTrackingSpeed <= kInputEpsilon) {
                        framingPitchVelocity = 0.0f;
                    } else {
                        pitch = SmoothDampScalar(
                            pitch,
                            pitchTarget,
                            framingPitchVelocity,
                            framingDynamics.pitchSmoothSeconds,
                            framingPitchTrackingSpeed *
                                clampedFramingWeight,
                            cameraDt);
                    }
                    lookIdleSeconds = 0.0f;
                } else {
                    framingYawVelocity = 0.0f;
                    framingPitchVelocity = 0.0f;
                }

                if (std::abs(zoom) > kInputEpsilon) {
                    const bool gamepadZoom = input != nullptr &&
                        input->GetLastActiveDevice() ==
                            INPUT::InputDeviceKind::Gamepad;
                    const float zoomAmount = gamepadZoom
                        ? component.GetZoomSpeedUnitsPerSecond() * cameraDt
                        : component.GetMouseWheelZoomUnitsPerStep();
                    desiredDistance -= zoom * zoomAmount;
                }
                desiredDistance = std::clamp(
                    desiredDistance,
                    component.GetMinimumDistance(),
                    component.GetMaximumDistance());

                const float targetSpeed = dt > kViewEpsilon
                    ? MATH::Length(
                        pivot - component.GetRuntimeTargetPivot()) / dt
                    : 0.0f;
                GAMEPLAY::MotionIntent motionIntent{};
                const bool movingBackward =
                    motionIntentService_ != nullptr &&
                    motionIntentService_->PeekIntent(
                        target->GetRuntimeHandle(),
                        frameIndex,
                        motionIntent) &&
                    motionIntent.move.y < kBackwardMovementThreshold;
                const bool automaticRecenter =
                    !hasTargetFraming &&
                    clampedFramingWeight <= kInputEpsilon &&
                    component.IsAutoRecenterEnabled() &&
                    !movingBackward &&
                    targetSpeed > 0.05f &&
                    lookIdleSeconds >=
                        component.GetAutoRecenterDelaySeconds();
                if (recenterPressed || automaticRecenter) {
                    const float recenterDelta = recenterPressed
                        ? 360.0f
                        : component.GetAutoRecenterSpeedDegreesPerSecond() *
                            cameraDt;
                    yaw = MoveTowardsAngle(
                        yaw,
                        ResolveTargetYawDegrees(targetWorld),
                        recenterDelta);
                }
                yaw = WrapDegrees(yaw);
                pitch = std::clamp(
                    pitch,
                    component.GetMinimumPitchDegrees(),
                    component.GetMaximumPitchDegrees());

                const float followAmount = SmoothFactor(
                    component.GetFollowSmooth(), dt);
                const MATH::Vec3 followPivot =
                    component.GetRuntimeFollowPivot() +
                    (pivot - component.GetRuntimeFollowPivot()) *
                        followAmount;
                MATH::Vec3 focusVelocity = hadFramingState
                    ? component.GetRuntimeFramingFocusVelocity()
                    : MATH::Vec3{};
                MATH::Vec3 focusOffset = hadFramingState
                    ? component.GetRuntimeFramingFocusOffset()
                    : MATH::Vec3{};
                const float focusWeight =
                    std::clamp(framingTargetWeight, 0.0f, 1.0f) *
                    clampedFramingWeight;
                const MATH::Vec3 desiredFocusOffset =
                    (secondaryPivot - followPivot) * focusWeight;
                focusOffset = SmoothDampVector(
                    focusOffset,
                    desiredFocusOffset,
                    focusVelocity,
                    framingDynamics.focusSmoothSeconds,
                    cameraDt);
                const bool focusResponseActive = framingResponseActive ||
                    MATH::Length(focusOffset) > kViewEpsilon ||
                    MATH::Length(focusVelocity) > kViewEpsilon;
                const MATH::Vec3 lookAt = focusResponseActive
                    ? followPivot + focusOffset
                    : component.GetRuntimeLookAt() +
                        (pivot - component.GetRuntimeLookAt()) *
                        SmoothFactor(component.GetLookSmooth(), dt);
                const MATH::Vec3 orbitDirection = OrbitDirection(yaw, pitch);

                float framingDistance = desiredDistance;
                if (secondaryTarget != nullptr) {
                    MATH::Vec3 separation = secondaryPivot - pivot;
                    separation.y = 0.0f;
                    const float separationLength = MATH::Length(separation);
                    const float scaledSeparation =
                        std::isfinite(separationLength)
                        ? separationLength * framingDistanceScale
                        : framingMaximumDistanceOffset;
                    const float distanceOffset = std::min(
                        framingMaximumDistanceOffset,
                        std::isfinite(scaledSeparation)
                        ? scaledSeparation
                        : framingMaximumDistanceOffset);
                    framingDistance = std::clamp(
                        desiredDistance +
                            distanceOffset * clampedFramingWeight,
                        component.GetMinimumDistance(),
                        component.GetMaximumDistance());
                }

                bool collisionLimited = false;
                const float collisionDistance = ResolveCollisionDistance(
                    physicsService_,
                    component,
                    *target,
                    followPivot,
                    orbitDirection,
                    framingDistance,
                    collisionLimited);
                float resolvedDistance = component.GetRuntimeDistance();
                float distanceVelocity = hadFramingState
                    ? component.GetRuntimeFramingDistanceVelocity()
                    : 0.0f;
                if (!std::isfinite(resolvedDistance)) {
                    resolvedDistance = framingDistance;
                    distanceVelocity = 0.0f;
                }
                const bool collisionPushIn = collisionLimited &&
                    collisionDistance < resolvedDistance;
                if (collisionPushIn) {
                    resolvedDistance = collisionDistance;
                    distanceVelocity = 0.0f;
                } else if (framingResponseActive ||
                    (component.WasCollisionLimited() &&
                        !collisionLimited)) {
                    const bool releasingCollision =
                        component.WasCollisionLimited() &&
                        !collisionLimited;
                    const bool expanding =
                        collisionDistance > resolvedDistance;
                    const float smoothSeconds = releasingCollision
                        ? framingDynamics.distanceContractSmoothSeconds
                        : (expanding
                            ? framingDynamics.distanceExpandSmoothSeconds
                            : framingDynamics.distanceContractSmoothSeconds);
                    resolvedDistance = SmoothDampScalar(
                        resolvedDistance,
                        collisionDistance,
                        distanceVelocity,
                        smoothSeconds,
                        -1.0f,
                        cameraDt);
                } else {
                    resolvedDistance +=
                        (collisionDistance - resolvedDistance) *
                        SmoothFactor(component.GetDistanceSmooth(), dt);
                    // The ordinary world-follow path owns distance directly;
                    // do not retain a stale framing spring velocity here.
                    distanceVelocity = 0.0f;
                }

                const MATH::Vec3 eye =
                    followPivot + orbitDirection * resolvedDistance;

                component.SetRuntimeOrbit(
                    yaw,
                    pitch,
                    desiredDistance,
                    resolvedDistance,
                    lookIdleSeconds);
                component.SetRuntimeView(
                    eye,
                    lookAt,
                    collisionLimited);
                component.SetRuntimeTracking(followPivot, pivot);
                component.SetRuntimeFramingState(
                    hasFramingSubmission,
                    framingTarget,
                    framingSourceId,
                    framingPriority,
                    framingDynamics,
                    framingTargetWeight,
                    framingDistanceScale,
                    framingMaximumDistanceOffset,
                    framingYawSpeed,
                    framingPreferredPitch,
                    framingPitchTrackingSpeed,
                    suppressOrbitInput,
                    suppressZoomInput,
                    framingSwitchActive,
                    framingSwitchRemainingSeconds,
                    secondaryPivot,
                    clampedFramingWeight,
                    framingWeightVelocity,
                    secondaryPivotVelocity,
                    framingYawVelocity,
                    framingPitchVelocity,
                    focusOffset,
                    focusVelocity,
                    distanceVelocity);

                CAMERA::CameraRigSubmission submission{};
                submission.cameraObjectId =
                    cameraObject.GetDocumentId();
                submission.sourceId = kFollowRigSource;
                submission.priority = component.GetPriority();
                submission.pose.eye = eye;
                submission.pose.target = lookAt;
                submission.pose.up = { 0.0f, 1.0f, 0.0f };
                (void)rigService_->SubmitPose(submission);
            });
    }

} // namespace HIKARI
