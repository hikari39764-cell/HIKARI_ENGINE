#include "HIKARI_CameraFollowSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "Core/HIKARI_FrameContext.h"
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
        const INPUT::InputSnapshot* input = inputService_ != nullptr
            ? &inputService_->GetSnapshot()
            : nullptr;

        world.ForEachObjectWith<CameraComponent, CameraFollowComponent>(
            [this, input, dt, frameIndex = frame.frameIndex](
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
                if (!component.HasRuntimeState()) {
                    const MATH::Vec3 direction = OrbitDirection(
                        component.GetInitialYawDegrees(),
                        component.GetInitialPitchDegrees());
                    component.InitializeRuntimeState(
                        pivot + direction * component.GetInitialDistance(),
                        pivot);
                }

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
                    if (!component.GetLookActionId().empty()) {
                        look = input->GetAxis2D(
                            component.GetLookActionId());
                    }
                    if (!component.GetZoomActionId().empty()) {
                        zoom = input->GetAxis1D(
                            component.GetZoomActionId());
                    }
                    if (!component.GetRecenterActionId().empty()) {
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
                        ? component.GetYawSpeedDegreesPerSecond() * dt
                        : component.GetMouseSensitivityDegreesPerPixel();
                    const float pitchAmount = gamepadLook
                        ? component.GetPitchSpeedDegreesPerSecond() * dt
                        : component.GetMouseSensitivityDegreesPerPixel();
                    // Orbit direction points from the target to the camera,
                    // so it turns opposite to the viewed direction.
                    yaw -= look[0] * yawAmount;
                    const float verticalSign =
                        component.GetInvertVerticalLook() ? -1.0f : 1.0f;
                    pitch += look[1] * verticalSign * pitchAmount;
                    lookIdleSeconds = 0.0f;
                } else {
                    lookIdleSeconds += dt;
                }

                if (std::abs(zoom) > kInputEpsilon) {
                    const bool gamepadZoom = input != nullptr &&
                        input->GetLastActiveDevice() ==
                            INPUT::InputDeviceKind::Gamepad;
                    const float zoomAmount = gamepadZoom
                        ? component.GetZoomSpeedUnitsPerSecond() * dt
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
                    component.IsAutoRecenterEnabled() &&
                    !movingBackward &&
                    targetSpeed > 0.05f &&
                    lookIdleSeconds >=
                        component.GetAutoRecenterDelaySeconds();
                if (recenterPressed || automaticRecenter) {
                    const float recenterDelta = recenterPressed
                        ? 360.0f
                        : component.GetAutoRecenterSpeedDegreesPerSecond() * dt;
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
                const float lookAmount = SmoothFactor(
                    component.GetLookSmooth(), dt);
                const MATH::Vec3 lookAt = component.GetRuntimeLookAt() +
                    (pivot - component.GetRuntimeLookAt()) * lookAmount;
                const MATH::Vec3 orbitDirection = OrbitDirection(yaw, pitch);

                bool collisionLimited = false;
                const float collisionDistance = ResolveCollisionDistance(
                    physicsService_,
                    component,
                    *target,
                    followPivot,
                    orbitDirection,
                    desiredDistance,
                    collisionLimited);
                float resolvedDistance = component.GetRuntimeDistance();
                if (collisionLimited &&
                    collisionDistance < resolvedDistance) {
                    resolvedDistance = collisionDistance;
                } else {
                    resolvedDistance +=
                        (collisionDistance - resolvedDistance) *
                        SmoothFactor(component.GetDistanceSmooth(), dt);
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
