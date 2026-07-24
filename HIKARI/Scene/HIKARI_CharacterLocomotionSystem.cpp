#include "Scene/HIKARI_CharacterLocomotionSystem.h"

#include <algorithm>
#include <cmath>

#include "Core/HIKARI_FrameContext.h"
#include "Gameplay/Motion/HIKARI_MotionIntentService.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Scene/Components/HIKARI_CharacterLocomotionComponent.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
    namespace {
        constexpr float kPi = 3.1415926535f;
        constexpr float kTwoPi = kPi * 2.0f;

        MATH::Vec3 Flatten(const MATH::Vec3& value) noexcept {
            return { value.x, 0.0f, value.z };
        }

        MATH::Vec3 FlattenAndNormalize(
            const MATH::Vec3& value) noexcept {
            const MATH::Vec3 flat = Flatten(value);
            return MATH::Length(flat) > 1.0e-5f
                ? MATH::Normalize(flat)
                : MATH::Vec3{};
        }

        MATH::Vec3 MoveTowards(
            const MATH::Vec3& current,
            const MATH::Vec3& target,
            float maximumDelta) noexcept {
            const MATH::Vec3 delta = target - current;
            const float length = MATH::Length(delta);
            if (length <= maximumDelta || length <= 1.0e-5f) {
                return target;
            }
            return current + delta * (maximumDelta / length);
        }

        float WrapAngle(float value) noexcept {
            while (value > kPi) value -= kTwoPi;
            while (value < -kPi) value += kTwoPi;
            return value;
        }

        float ExtractYaw(const MATH::Quat& rotation) noexcept {
            const MATH::Mat4 matrix = MATH::Mat4::Rotate(
                MATH::NormalizeQ(rotation));
            const MATH::Vec3 forward = FlattenAndNormalize({
                matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]
            });
            return MATH::Length(forward) > 1.0e-5f
                ? std::atan2(forward.x, forward.z)
                : 0.0f;
        }

        MATH::Quat RotateTowards(
            const MATH::Quat& current,
            const MATH::Vec3& direction,
            float maximumRadians) noexcept {
            if (MATH::Length(direction) <= 1.0e-5f) {
                return current;
            }
            const float targetYaw = std::atan2(
                direction.x,
                direction.z);
            const float currentYaw = ExtractYaw(current);
            const float delta = WrapAngle(targetYaw - currentYaw);
            const float step = std::clamp(
                delta,
                -maximumRadians,
                maximumRadians);
            return MATH::Quat::FromEulerXYZ(
                0.0f,
                currentYaw + step,
                0.0f);
        }

        MATH::Vec3 ResolveMovementDirection(
            CharacterMovementSpace space,
            const Camera3D* camera,
            const MATH::Quat& objectRotation,
            const MATH::Vec2& input,
            float deadZone) noexcept {
            MATH::Vec3 direction{ input.x, 0.0f, input.y };
            const float length = MATH::Length(direction);
            if (length <= deadZone) {
                return {};
            }
            if (length > 1.0f) {
                direction = direction * (1.0f / length);
            }
            if (space == CharacterMovementSpace::World ||
                (space == CharacterMovementSpace::Camera &&
                    camera == nullptr)) {
                return direction;
            }

            MATH::Vec3 right{};
            MATH::Vec3 forward{};
            if (space == CharacterMovementSpace::Camera &&
                camera != nullptr) {
                // Author camera-relative movement from the viewed direction
                // directly. This avoids leaking view-matrix conventions into
                // gameplay and guarantees W follows the screen forward axis.
                forward = FlattenAndNormalize(
                    camera->GetTarget() - camera->GetPosition());
                if (MATH::Length(forward) > 1.0e-5f) {
                    right = FlattenAndNormalize(MATH::Cross(
                        { 0.0f, 1.0f, 0.0f },
                        forward));
                }
                if (MATH::Length(right) <= 1.0e-5f ||
                    MATH::Length(forward) <= 1.0e-5f) {
                    right = { 1.0f, 0.0f, 0.0f };
                    forward = { 0.0f, 0.0f, 1.0f };
                }
            } else {
                const MATH::Mat4 basis = MATH::Mat4::Rotate(
                    objectRotation);
                right = FlattenAndNormalize({
                    basis.m[0][0], basis.m[0][1], basis.m[0][2]
                });
                forward = FlattenAndNormalize({
                    basis.m[2][0], basis.m[2][1], basis.m[2][2]
                });
            }
            const MATH::Vec3 world = right * direction.x +
                forward * direction.z;
            const float worldLength = MATH::Length(world);
            return worldLength > 1.0f
                ? world * (1.0f / worldLength)
                : world;
        }
    }

    void CharacterLocomotionSystem::OnWorldAttached(World& world) {
        motionIntentService_ = world.Services().Find<
            GAMEPLAY::MotionIntentService>();
        kinematicMotionService_ = world.Services().Find<
            PHYSICS::KinematicMotionService>();
        physicsService_ = world.Services().Find<
            PHYSICS::PhysicsWorldService>();
        cameraService_ = world.Services().Find<GameplayCameraService>();
    }

    void CharacterLocomotionSystem::OnWorldDetached(World&) {
        motionIntentService_ = nullptr;
        kinematicMotionService_ = nullptr;
        physicsService_ = nullptr;
        cameraService_ = nullptr;
        runtimeStates_.clear();
    }

    void CharacterLocomotionSystem::FixedUpdate(
        World& world,
        const FrameContext& frame) {
        if (motionIntentService_ == nullptr ||
            kinematicMotionService_ == nullptr ||
            frame.fixedDt <= 0.0f) {
            return;
        }
        const Camera3D* camera = cameraService_ != nullptr
            ? cameraService_->camera
            : nullptr;
        const MATH::Vec3 worldGravity = physicsService_ != nullptr
            ? physicsService_->GetSettings().gravity
            : MATH::Vec3{ 0.0f, -9.81f, 0.0f };

        world.ForEachObjectWith<
            CharacterLocomotionComponent,
            PhysicsBodyComponent>(
            [this, &frame, camera, worldGravity](
                GameObject& object,
                CharacterLocomotionComponent& locomotion,
                PhysicsBodyComponent& physicsBody) {
                const uint64_t runtimeKey =
                    object.GetRuntimeHandle().ToValue();
                GAMEPLAY::MotionIntent intent{};
                if (locomotion.IsEnabled()) {
                    (void)motionIntentService_->ResolveIntent(
                        object.GetRuntimeHandle(),
                        frame.frameIndex,
                        intent);
                } else {
                    runtimeStates_.erase(runtimeKey);
                    return;
                }

                const PHYSICS::KinematicMotionState* state =
                    kinematicMotionService_->FindState(
                        object.GetRuntimeHandle());
                const MATH::Quat currentRotation = state != nullptr
                    ? state->pose.rotation
                    : object.GetTransform().rotation;
                const bool grounded = state != nullptr &&
                    state->IsGrounded();
                RuntimeState& runtime = runtimeStates_[runtimeKey];
                runtime.lastTouchedFixedTick = frame.fixedTickIndex;
                const bool jumpPressed = intent.jumpPressed ||
                    (intent.jumpHeld &&
                        !runtime.jumpHeldLastFixedTick);
                runtime.jumpHeldLastFixedTick = intent.jumpHeld;
                if (grounded) {
                    runtime.groundGraceRemaining =
                        locomotion.GetGroundGraceSeconds();
                } else {
                    runtime.groundGraceRemaining = (std::max)(
                        0.0f,
                        runtime.groundGraceRemaining - frame.fixedDt);
                }
                if (jumpPressed) {
                    runtime.jumpBufferRemaining =
                        locomotion.GetJumpBufferSeconds();
                } else {
                    runtime.jumpBufferRemaining = (std::max)(
                        0.0f,
                        runtime.jumpBufferRemaining - frame.fixedDt);
                }
                const bool hasJumpRequest = jumpPressed ||
                    runtime.jumpBufferRemaining > 0.0f;
                const bool hasGroundPermission = grounded ||
                    runtime.groundGraceRemaining > 0.0f;
                const bool allowJumpWithoutGroundContact =
                    hasJumpRequest && hasGroundPermission;
                if (allowJumpWithoutGroundContact) {
                    runtime.jumpBufferRemaining = 0.0f;
                    runtime.groundGraceRemaining = 0.0f;
                }
                const MATH::Vec3 currentHorizontal = state != nullptr
                    ? Flatten(
                        state->velocity -
                        (grounded
                            ? state->groundVelocity
                            : MATH::Vec3{}))
                    : MATH::Vec3{};
                const MATH::Vec3 direction = ResolveMovementDirection(
                    locomotion.GetMovementSpace(),
                    camera,
                    currentRotation,
                    intent.move,
                    locomotion.GetInputDeadZone());

                const float speed = locomotion.GetMaximumSpeed() *
                    (intent.sprintHeld
                        ? locomotion.GetSprintMultiplier()
                        : 1.0f);
                const MATH::Vec3 targetVelocity = direction * speed;
                const bool hasMovementInput =
                    direction.x != 0.0f || direction.z != 0.0f;
                const float acceleration = hasMovementInput
                    ? (grounded
                        ? locomotion.GetAcceleration()
                        : locomotion.GetAirAcceleration())
                    : (grounded
                        ? locomotion.GetDeceleration()
                        : locomotion.GetAirDeceleration());

                PHYSICS::KinematicMotionRequest request{};
                request.horizontalVelocity = MoveTowards(
                    currentHorizontal,
                    targetVelocity,
                    acceleration * frame.fixedDt);
                request.maximumFallSpeed =
                    locomotion.GetMaximumFallSpeed();
                const float gravityMagnitude = (std::max)(
                    std::abs(worldGravity.y) *
                        physicsBody.GetGravityScale(),
                    0.001f);
                request.jumpSpeed = std::sqrt(
                    2.0f * gravityMagnitude *
                    locomotion.GetJumpHeight());
                request.jumpRequested = hasJumpRequest;
                request.allowJumpWithoutGroundContact =
                    allowJumpWithoutGroundContact;
                if (locomotion.GetRotateToMove() &&
                    MATH::Length(direction) > 1.0e-5f) {
                    request.desiredRotation = RotateTowards(
                        currentRotation,
                        direction,
                        locomotion.GetTurnSpeedRadians() *
                            frame.fixedDt);
                    request.hasDesiredRotation = true;
                }
                request.controller.maximumSlopeAngleRadians =
                    locomotion.GetMaximumSlopeAngleRadians();
                request.controller.stepUpHeight =
                    locomotion.GetStepHeight();
                request.controller.stickToFloorDistance =
                    locomotion.GetStickToFloorDistance();
                request.controller.stepForwardTestDistance =
                    locomotion.GetStepForwardTestDistance();
                request.controller.characterPadding =
                    locomotion.GetCharacterPadding();
                request.controller.predictiveContactDistance =
                    locomotion.GetPredictiveContactDistance();
                request.controller.penetrationRecoverySpeed =
                    locomotion.GetPenetrationRecoverySpeed();
                request.controller.maximumCollisionHits =
                    locomotion.GetMaximumCollisionHits();
                request.controller.enhancedInternalEdgeRemoval =
                    locomotion.GetEnhancedInternalEdgeRemoval();
                kinematicMotionService_->SubmitMove(
                    object.GetRuntimeHandle(),
                    PHYSICS::kCharacterLocomotionMotionSource,
                    locomotion.GetMotionPriority(),
                    request,
                    frame.fixedTickIndex);
            });

        for (auto runtime = runtimeStates_.begin();
            runtime != runtimeStates_.end();) {
            if (runtime->second.lastTouchedFixedTick !=
                    frame.fixedTickIndex) {
                runtime = runtimeStates_.erase(runtime);
            } else {
                ++runtime;
            }
        }
    }

} // namespace HIKARI
