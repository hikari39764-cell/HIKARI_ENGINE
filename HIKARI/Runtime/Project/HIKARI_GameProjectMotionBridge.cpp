#include "HIKARI/Project/ActionViews.h"
#include "Core/Numeric/HIKARI_NonZero.h"
#include "HIKARI/Project/WorldView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectActionValidation.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace HIKARI {
using namespace PROJECT::INTERNAL;

MotionView GameProjectSdkAccess::MakeMotionView(World& world) noexcept { return MotionView(&world); }

MotionView WorldView::GetMotion() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeMotionView(*world) : MotionView{};
}

bool MotionView::SubmitKinematicMove(const GameProjectKinematicMove& move) const noexcept {
    return SubmitKinematicMoveWithOptions(move, {});
}

bool MotionView::SubmitKinematicMoveWithCollisionOptions(
    const GameProjectKinematicMove& move, const GameProjectKinematicCollisionOptions& options) const noexcept {
    GameProjectKinematicMoveOptions combined;
    combined.collision = options;
    return SubmitKinematicMoveWithOptions(move, combined);
}

bool MotionView::SubmitKinematicMoveWithOptions(const GameProjectKinematicMove& move,
                                                const GameProjectKinematicMoveOptions& options) const noexcept {
    try {
        if (world_ == nullptr || move.sourceId == 0u || !IsFinite(move.horizontalVelocity) ||
            !IsFinite(move.desiredRotation) || !IsFiniteNonNegative(move.jumpSpeed) ||
            !IsFinitePositive(move.maximumFallSpeed) || options.structSize != sizeof(options) ||
            options.version != 1u || options.collision.reserved0 != 0u || options.collision.reserved1 != 0u ||
            options.overrideVerticalGravity > 1u || !std::isfinite(options.verticalGravityAcceleration) ||
            (move.hasDesiredRotation != 0u && !IsFiniteNonZeroRotation(move.desiredRotation))) {
            return false;
        }
        World* world = CurrentWorld(world_);
        if (world == nullptr || GameProjectSdkAccess::IsWorldClosing(*world))
            return false;
        GameObject* object = FindObject(world, move.objectHandle);
        if (object == nullptr) {
            return false;
        }
        GameObject* ignoredObject = options.collision.ignoredObjectHandle != 0u
                                        ? FindObject(world, options.collision.ignoredObjectHandle)
                                        : nullptr;
        if (options.collision.ignoredObjectHandle != 0u && (ignoredObject == nullptr || ignoredObject == object)) {
            return false;
        }
        PHYSICS::KinematicMotionService* service = world->Services().Find<PHYSICS::KinematicMotionService>();
        if (service == nullptr) {
            return false;
        }
        PHYSICS::KinematicMotionRequest request{};
        request.horizontalVelocity = ToRuntime(move.horizontalVelocity);
        request.desiredRotation = ToRuntime(move.desiredRotation);
        request.jumpSpeed = move.jumpSpeed;
        request.maximumFallSpeed = move.maximumFallSpeed;
        request.verticalGravityAcceleration = options.verticalGravityAcceleration;
        request.overrideVerticalGravity = options.overrideVerticalGravity != 0u;
        request.hasDesiredRotation = move.hasDesiredRotation != 0u;
        request.jumpRequested = move.jumpRequested != 0u;
        request.allowJumpWithoutGroundContact = move.allowJumpWithoutGroundContact != 0u;
        request.ignoredCollisionObject =
            ignoredObject != nullptr ? ignoredObject->GetRuntimeHandle() : RuntimeObjectHandle{};
        service->SubmitMove(object->GetRuntimeHandle(), move.sourceId, move.priority, request, move.fixedTickIndex);
        return true;
    } catch (...) {
        return false;
    }
}

bool MotionView::SubmitMotionDelta(const GameProjectKinematicMotionDelta& delta) const noexcept {
    try {
        if (world_ == nullptr || delta.sourceId == 0u || !IsFinite(delta.translation) ||
            !IsFiniteNonZeroRotation(delta.rotation)) {
            return false;
        }
        World* world = static_cast<World*>(world_);
        GameObject* object = FindObject(world, delta.objectHandle);
        if (object == nullptr) {
            return false;
        }
        PHYSICS::KinematicMotionService* service = world->Services().Find<PHYSICS::KinematicMotionService>();
        return service != nullptr && service->SubmitMotionDelta(
                                         object->GetRuntimeHandle(), delta.sourceId, delta.priority,
                                         delta.fixedTickIndex, ToRuntime(delta.translation), ToRuntime(delta.rotation));
    } catch (...) {
        return false;
    }
}

bool MotionView::RequestTeleport(const GameProjectKinematicTeleport& teleport) const noexcept {
    try {
        if (world_ == nullptr || teleport.sourceId == 0u || !IsFinite(teleport.position) ||
            !IsFiniteNonZeroRotation(teleport.rotation) || NUMERIC::AnyNonZero(teleport.reserved)) {
            return false;
        }
        World* world = static_cast<World*>(world_);
        GameObject* object = FindObject(world, teleport.objectHandle);
        PHYSICS::KinematicMotionService* service = world->Services().Find<PHYSICS::KinematicMotionService>();
        if (object == nullptr || service == nullptr) {
            return false;
        }
        PHYSICS::KinematicTeleportRequest request{};
        request.pose.position = ToRuntime(teleport.position);
        request.pose.rotation = ToRuntime(teleport.rotation);
        request.clearVelocity = teleport.clearVelocity != 0u;
        service->RequestTeleport(object->GetRuntimeHandle(), teleport.sourceId, teleport.priority, request);
        return true;
    } catch (...) {
        return false;
    }
}

bool MotionView::GetKinematicState(uint64_t objectHandle, GameProjectKinematicState& outState) const noexcept {
    outState = {};
    try {
        World* world = static_cast<World*>(world_);
        GameObject* object = FindObject(world, objectHandle);
        if (object == nullptr) {
            return false;
        }
        const PHYSICS::KinematicMotionService* service = world->Services().Find<PHYSICS::KinematicMotionService>();
        const PHYSICS::KinematicMotionState* state =
            service != nullptr ? service->FindState(object->GetRuntimeHandle()) : nullptr;
        if (state == nullptr) {
            return false;
        }
        outState.pose.position = FiniteOr(state->pose.position);
        outState.pose.rotation = FiniteOr(state->pose.rotation);
        outState.pose.scale = FiniteOr(object->GetTransform().scale, {1.0f, 1.0f, 1.0f});
        outState.velocity = FiniteOr(state->velocity);
        outState.groundVelocity = FiniteOr(state->groundVelocity);
        outState.groundPosition = FiniteOr(state->groundPosition);
        outState.groundNormal = FiniteOr(state->groundNormal, {0.0f, 1.0f, 0.0f});
        outState.groundObjectHandle = state->groundObject.ToValue();
        switch (state->groundState) {
        case PHYSICS::PhysicsCharacterGroundState::OnGround:
            outState.groundState = GameProjectGroundState::OnGround;
            break;
        case PHYSICS::PhysicsCharacterGroundState::OnSteepGround:
            outState.groundState = GameProjectGroundState::OnSteepGround;
            break;
        case PHYSICS::PhysicsCharacterGroundState::NotSupported:
            outState.groundState = GameProjectGroundState::NotSupported;
            break;
        case PHYSICS::PhysicsCharacterGroundState::InAir:
        default: outState.groundState = GameProjectGroundState::InAir; break;
        }
        outState.grounded = state->IsGrounded() ? 1u : 0u;
        outState.hitWall = state->hitWall ? 1u : 0u;
        outState.hitCeiling = state->hitCeiling ? 1u : 0u;
        outState.maximumHitsExceeded = state->maximumHitsExceeded ? 1u : 0u;
        return true;
    } catch (...) {
        outState = {};
        return false;
    }
}
} // namespace HIKARI
