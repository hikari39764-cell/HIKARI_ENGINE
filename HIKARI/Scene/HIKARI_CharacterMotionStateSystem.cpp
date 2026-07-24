#include "Scene/HIKARI_CharacterMotionStateSystem.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

#include "Core/HIKARI_FrameContext.h"
#include "Gameplay/Motion/HIKARI_CharacterMotionStateService.h"
#include "Gameplay/Motion/HIKARI_MotionIntentService.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Scene/Components/HIKARI_CharacterLocomotionComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    void CharacterMotionStateSystem::OnWorldAttached(World& world) {
        publishedObjects_.clear();
        stateService_ = world.Services().Find<
            GAMEPLAY::CharacterMotionStateService>();
        motionIntentService_ = world.Services().Find<
            GAMEPLAY::MotionIntentService>();
        kinematicMotionService_ = world.Services().Find<
            PHYSICS::KinematicMotionService>();
    }

    void CharacterMotionStateSystem::OnWorldDetached(World&) {
        if (stateService_ != nullptr) stateService_->Clear();
        stateService_ = nullptr;
        motionIntentService_ = nullptr;
        kinematicMotionService_ = nullptr;
        publishedObjects_.clear();
    }

    void CharacterMotionStateSystem::PostFixedUpdate(
        World& world,
        const FrameContext& frame) {
        if (stateService_ == nullptr ||
            kinematicMotionService_ == nullptr) {
            return;
        }

        std::unordered_set<uint64_t> currentObjectKeys{};
        std::vector<RuntimeObjectHandle> currentObjects{};

        world.ForEachObjectWith<CharacterLocomotionComponent>(
            [this, &frame, &currentObjectKeys, &currentObjects](
                GameObject& object,
                CharacterLocomotionComponent& locomotion) {
                if (!locomotion.IsEnabled()) {
                    stateService_->Remove(object.GetRuntimeHandle());
                    return;
                }
                const PHYSICS::KinematicMotionState* physics =
                    kinematicMotionService_->FindState(
                        object.GetRuntimeHandle());
                if (physics == nullptr) {
                    stateService_->Remove(object.GetRuntimeHandle());
                    return;
                }

                GAMEPLAY::MotionIntent intent{};
                if (motionIntentService_ != nullptr) {
                    (void)motionIntentService_->PeekIntent(
                        object.GetRuntimeHandle(),
                        frame.frameIndex,
                        intent);
                }

                GAMEPLAY::CharacterMotionState state{};
                state.object = object.GetRuntimeHandle();
                state.worldVelocity = physics->velocity;
                state.groundVelocity = physics->groundVelocity;
                state.horizontalVelocity = {
                    physics->velocity.x,
                    0.0f,
                    physics->velocity.z
                };
                state.horizontalSpeed = MATH::Length(
                    state.horizontalVelocity);
                state.verticalSpeed = physics->velocity.y;
                state.inputMagnitude = std::clamp(
                    std::sqrt(
                        intent.move.x * intent.move.x +
                        intent.move.y * intent.move.y),
                    0.0f,
                    1.0f);
                state.groundNormal = physics->groundNormal;
                state.groundState = physics->groundState;
                state.fixedTickIndex = frame.fixedTickIndex;
                state.moving = state.horizontalSpeed > 1.0e-3f;
                state.sprinting = intent.sprintHeld;
                state.hitWall = physics->hitWall;
                state.hitCeiling = physics->hitCeiling;
                stateService_->Publish(state);
                currentObjectKeys.insert(
                    object.GetRuntimeHandle().ToValue());
                currentObjects.push_back(object.GetRuntimeHandle());
            });

        for (const RuntimeObjectHandle object : publishedObjects_) {
            if (!currentObjectKeys.contains(object.ToValue())) {
                stateService_->Remove(object);
            }
        }
        publishedObjects_ = std::move(currentObjects);
    }

} // namespace HIKARI
