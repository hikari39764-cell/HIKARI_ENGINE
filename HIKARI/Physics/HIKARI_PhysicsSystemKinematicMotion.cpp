#include "Physics/HIKARI_PhysicsSystem.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "Core/HIKARI_FrameContext.h"
#include "Physics/HIKARI_KinematicMotionDiagnostics.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Physics/HIKARI_PhysicsSceneBridge.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::PHYSICS {
    namespace {
        KinematicMotionState ToMotionState(
            const PhysicsCharacterState& source) noexcept {
            KinematicMotionState result{};
            result.pose = source.pose;
            result.velocity = source.linearVelocity;
            result.groundVelocity = source.groundVelocity;
            result.groundPosition = source.groundPosition;
            result.groundNormal = source.groundNormal;
            result.groundObject = source.groundObject;
            result.groundState = source.groundState;
            result.hitWall = source.hitWall;
            result.hitCeiling = source.hitCeiling;
            result.maximumHitsExceeded = source.maxHitsExceeded;
            return result;
        }

    }

    void PhysicsSystem::ProcessKinematicMotions(
        World& world,
        const FrameContext& frame) {
        if (service_ == nullptr || kinematicMotion_ == nullptr ||
            frame.fixedDt <= 0.0f) {
            return;
        }

        for (auto& [_, binding] : bindings_) {
            KinematicMotionRequest request{};
            const bool hasMove = kinematicMotion_->ConsumeMove(
                binding.object,
                frame.fixedTickIndex,
                request);
            KinematicTeleportRequest teleport{};
            const bool hasTeleport = kinematicMotion_->ConsumeTeleport(
                binding.object,
                teleport);
            if (!hasMove && !hasTeleport) {
                if (binding.effectiveMotionType ==
                        PhysicsMotionType::Kinematic) {
                    binding.kinematicPresentationActive = false;
                }
                continue;
            }

            const auto setError = [this, &binding](std::string message) {
                kinematicMotion_->SetStatus(
                    binding.object,
                    {
                        KinematicMotionRuntimeHealth::Error,
                        std::move(message)
                    });
            };

            if (binding.effectiveMotionType !=
                    PhysicsMotionType::Kinematic) {
                setError(
                    "Physics Body must use Kinematic motion for controller-driven movement");
                continue;
            }
            if (!service_->GetCapabilities().virtualCharacters) {
                setError(
                    "the active physics backend has no kinematic character solver");
                continue;
            }

            GameObject* object = world.FindObject(binding.object);
            if (object == nullptr || !binding.body.IsValid()) {
                setError("physics body or scene object is unavailable");
                continue;
            }

            if (hasTeleport) {
                if (!service_->SetBodyPose(
                        binding.body,
                        teleport.pose,
                        false) ||
                    !ApplyPhysicsWorldPose(*object, teleport.pose)) {
                    setError("unable to teleport the kinematic body");
                    continue;
                }
                binding.lastPushedPose = teleport.pose;
                binding.hasLastPushedPose = true;
                PhysicsBodyState teleportedState =
                    binding.hasFixedState
                    ? binding.currentFixedState
                    : PhysicsBodyState{};
                teleportedState.pose = teleport.pose;
                teleportedState.awake = true;
                if (teleport.clearVelocity) {
                    teleportedState.linearVelocity = {};
                    teleportedState.angularVelocity = {};
                }
                CommitFixedState(
                    binding,
                    teleportedState,
                    true);
                binding.kinematicPresentationActive = true;
                if (binding.kinematicSolver.IsValid()) {
                    (void)service_->SetCharacterPose(
                        binding.kinematicSolver,
                        teleport.pose);
                    if (teleport.clearVelocity) {
                        (void)service_->SetCharacterVelocity(
                            binding.kinematicSolver,
                            {});
                    }
                }
            }
            if (!hasMove) {
                continue;
            }

            const bool jumpActiveAtEntry =
                binding.kinematicJumpActive;
            const PhysicsCharacterHandle solverBeforeEnsure =
                binding.kinematicSolver;
            const uint64_t solverSignatureBeforeEnsure =
                binding.kinematicSolverSignature;

            PhysicsBodyState bodyState{};
            if (!service_->TryGetBodyState(binding.body, bodyState)) {
                setError("unable to read the kinematic physics body");
                continue;
            }
            if (hasTeleport) {
                bodyState.pose = teleport.pose;
                if (teleport.clearVelocity) {
                    bodyState.linearVelocity = {};
                    bodyState.angularVelocity = {};
                }
            }

            bool retainedPreviousSolver = false;
            std::string solverError{};
            if (!EnsureKinematicSolver(
                    binding,
                    request.controller,
                    bodyState,
                    retainedPreviousSolver,
                    solverError)) {
                setError(std::move(solverError));
                continue;
            }

            PhysicsCharacterState solverStateBeforeSync{};
            const bool shouldTraceJump = request.jumpRequested ||
                jumpActiveAtEntry || binding.kinematicJumpActive;
            const bool hasSolverStateBeforeSync = shouldTraceJump &&
                service_->TryGetCharacterState(
                    binding.kinematicSolver,
                    solverStateBeforeSync);

            if (!service_->SetCharacterPose(
                    binding.kinematicSolver,
                    bodyState.pose)) {
                setError("unable to synchronize the kinematic motion solver");
                continue;
            }
            if (hasTeleport && teleport.clearVelocity) {
                (void)service_->SetCharacterVelocity(
                    binding.kinematicSolver,
                    {});
                binding.kinematicJumpActive = false;
            }
            (void)service_->RefreshCharacterContacts(
                binding.kinematicSolver);
            (void)service_->RefreshCharacterGroundVelocity(
                binding.kinematicSolver);

            PhysicsCharacterState before{};
            if (!service_->TryGetCharacterState(
                    binding.kinematicSolver,
                    before)) {
                setError("unable to read the kinematic motion state");
                continue;
            }

            if (request.hasDesiredRotation) {
                PhysicsPose solverPose = before.pose;
                solverPose.rotation = request.desiredRotation;
                if (!service_->SetCharacterPose(
                        binding.kinematicSolver,
                        solverPose)) {
                    setError("unable to rotate the kinematic motion solver");
                    continue;
                }
                before.pose.rotation = request.desiredRotation;
            }

            const MATH::Vec3 gravity = settings_.gravity *
                binding.bodyDesc.gravityScale;
            const bool jumpActiveBeforeStep =
                binding.kinematicJumpActive;
            const bool onGround = before.IsGrounded();
            constexpr float kGroundDetachSpeed = 0.1f;
            const float relativeVertical =
                before.linearVelocity.y - before.groundVelocity.y;
            MATH::Vec3 velocity{};
            if (onGround && !binding.kinematicJumpActive &&
                relativeVertical < kGroundDetachSpeed) {
                velocity = before.groundVelocity +
                    request.horizontalVelocity;
            } else {
                velocity = request.horizontalVelocity;
                velocity.y = before.linearVelocity.y;
            }
            const bool canApplyJump = onGround ||
                request.allowJumpWithoutGroundContact;
            bool jumpApplied = false;
            if (request.jumpRequested && canApplyJump &&
                request.jumpSpeed > 0.0f) {
                velocity.y = request.jumpSpeed +
                    (onGround ? before.groundVelocity.y : 0.0f);
                jumpApplied = true;
                binding.kinematicJumpActive = true;
            }
            velocity = velocity + gravity * frame.fixedDt;
            if (request.maximumFallSpeed > 0.0f) {
                velocity.y = (std::max)(
                    velocity.y,
                    -request.maximumFallSpeed);
            }
            velocity = velocity +
                kinematicMotion_->ConsumeExternalVelocity(
                    binding.object);

            const float supportVerticalVelocity = onGround
                ? before.groundVelocity.y
                : 0.0f;
            const bool ascendingAwayFromGround = jumpApplied ||
                velocity.y - supportVerticalVelocity >
                    kGroundDetachSpeed;

            if (!service_->SetCharacterVelocity(
                    binding.kinematicSolver,
                    velocity)) {
                setError("unable to update kinematic motion velocity");
                continue;
            }
            PhysicsCharacterStepSettings step{};
            step.gravity = gravity;
            // Step and floor-snap helpers are ground traversal policies. A
            // jump keeps them disabled for its complete airborne lifecycle,
            // including descent, until the solver confirms a real landing.
            const bool allowGroundTraversal = onGround &&
                !binding.kinematicJumpActive &&
                !ascendingAwayFromGround;
            if (allowGroundTraversal) {
                step.stepUpHeight = request.controller.stepUpHeight;
                step.stickToFloorDistance =
                    request.controller.stickToFloorDistance;
                step.stepForwardTestDistance =
                    request.controller.stepForwardTestDistance;
            }
            if (!service_->StepCharacter(
                    binding.kinematicSolver,
                    frame.fixedDt,
                    step)) {
                setError("kinematic motion solve failed");
                continue;
            }

            PhysicsCharacterState after{};
            if (!service_->TryGetCharacterState(
                    binding.kinematicSolver,
                    after)) {
                setError("unable to read the kinematic motion result");
                continue;
            }
            if (binding.kinematicJumpActive && after.hitCeiling &&
                after.linearVelocity.y > 0.0f) {
                after.linearVelocity.y = 0.0f;
                (void)service_->SetCharacterVelocity(
                    binding.kinematicSolver,
                    after.linearVelocity);
            }
            const float landingRelativeVertical =
                after.linearVelocity.y - after.groundVelocity.y;
            if (binding.kinematicJumpActive && !jumpApplied &&
                after.IsGrounded() &&
                landingRelativeVertical < kGroundDetachSpeed) {
                binding.kinematicJumpActive = false;
            }
            TraceKinematicMotionStep({
                object->GetName(),
                binding.object,
                binding.body,
                solverBeforeEnsure,
                binding.kinematicSolver,
                solverSignatureBeforeEnsure,
                binding.kinematicSolverSignature,
                frame,
                bodyState,
                solverStateBeforeSync,
                before,
                after,
                request,
                gravity,
                velocity,
                hasSolverStateBeforeSync,
                retainedPreviousSolver,
                jumpActiveAtEntry,
                jumpActiveBeforeStep,
                jumpApplied,
                binding.kinematicJumpActive,
                allowGroundTraversal
            });
            if (!service_->SetKinematicTarget(
                    binding.body,
                    after.pose) ||
                !ApplyPhysicsWorldPose(*object, after.pose)) {
                setError("unable to apply the solved kinematic body pose");
                continue;
            }

            binding.lastPushedPose = after.pose;
            binding.hasLastPushedPose = true;
            PhysicsBodyState solvedState = bodyState;
            solvedState.pose = after.pose;
            solvedState.linearVelocity = after.linearVelocity;
            solvedState.awake = true;
            CommitFixedState(binding, solvedState, false);
            binding.kinematicPresentationActive = true;

            kinematicMotion_->SetState(
                binding.object,
                ToMotionState(after));
            kinematicMotion_->SetStatus(
                binding.object,
                {
                    retainedPreviousSolver
                        ? KinematicMotionRuntimeHealth::RetainedPrevious
                        : KinematicMotionRuntimeHealth::Ready,
                    retainedPreviousSolver
                        ? "previous kinematic solver retained"
                        : "kinematic motion ready"
                });

            const bool wasGrounded = before.IsGrounded();
            const bool isGrounded = after.IsGrounded();
            if (!wasGrounded && isGrounded) {
                world.FixedEvents().Publish(KinematicBodyLandedEvent{
                    binding.object,
                    after.groundObject,
                    after.groundPosition,
                    after.groundNormal,
                    (std::max)(
                        0.0f,
                        -(before.linearVelocity.y -
                            after.groundVelocity.y))
                });
            } else if (wasGrounded && !isGrounded) {
                world.FixedEvents().Publish(
                    KinematicBodyLeftGroundEvent{
                        binding.object,
                        before.groundObject
                    });
            }
        }
    }

} // namespace HIKARI::PHYSICS
