#include "Physics/HIKARI_PhysicsSystem.h"

#include <utility>

#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Physics/HIKARI_PhysicsPresentation.h"
#include "Physics/HIKARI_PhysicsSceneBridge.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/HIKARI_RuntimeObjectActivityService.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::PHYSICS {
void PhysicsSystem::PushSceneDrivenPoses(World& world) {
    if (service_ == nullptr) {
        return;
    }
    for (auto& [_, binding] : bindings_) {
        if (binding.effectiveMotionType == PhysicsMotionType::Dynamic || !binding.body.IsValid()) {
            continue;
        }
        const GameObject* object = world.FindObject(binding.object);
        if (object == nullptr) {
            continue;
        }
        PhysicsPose pose{};
        MATH::Vec3 scale{};
        const bool kinematic = binding.effectiveMotionType == PhysicsMotionType::Kinematic;
        if (!TryGetPhysicsWorldPoseAndScale(*object, pose, scale) ||
            (!kinematic && binding.hasLastPushedPose && ArePhysicsPosesNearlyEqual(binding.lastPushedPose, pose))) {
            continue;
        }
        // 停止した Kinematic にも同じ目標を渡し、前 tick の速度を残さない。
        const bool pushed = binding.effectiveMotionType == PhysicsMotionType::Kinematic
                                ? service_->SetKinematicTarget(binding.body, pose)
                                : service_->SetBodyPose(binding.body, pose, false);
        if (pushed) {
            binding.lastPushedPose = pose;
            binding.hasLastPushedPose = true;
        }
    }
}

void PhysicsSystem::CaptureSimulatedPoses(World& world) {
    if (service_ == nullptr) {
        return;
    }
    for (auto& [_, binding] : bindings_) {
        if (binding.effectiveMotionType == PhysicsMotionType::Static || !binding.body.IsValid()) {
            continue;
        }
        GameObject* object = world.FindObject(binding.object);
        PhysicsBodyState state{};
        if (object == nullptr || !service_->TryGetBodyState(binding.body, state)) {
            continue;
        }
        CommitFixedState(binding, state, false);
        // Kinematic の権威座標はゲーム／controller が所有し、表示姿勢を戻さない。
        if (binding.effectiveMotionType == PhysicsMotionType::Dynamic)
            (void)ApplyPhysicsWorldPose(*object, state.pose);

        if (runtimeStatus_ != nullptr) {
            const PhysicsBodyRuntimeStatus* existing = runtimeStatus_->FindBodyStatus(binding.object);
            if (existing != nullptr && existing->awake != state.awake) {
                PhysicsBodyRuntimeStatus updated = *existing;
                updated.awake = state.awake;
                runtimeStatus_->SetBodyStatus(std::move(updated));
            }
        }
    }
}

void PhysicsSystem::CommitFixedState(BodyBinding& binding, const PhysicsBodyState& state, bool discontinuity) noexcept {
    if (!binding.hasFixedState || discontinuity) {
        binding.previousFixedState = state;
    } else {
        binding.previousFixedState = binding.currentFixedState;
    }
    binding.currentFixedState = state;
    binding.hasFixedState = true;
    binding.presentationDiscontinuity = binding.presentationDiscontinuity || discontinuity;
}

void PhysicsSystem::UpdatePresentationPoses(World& world, float interpolationAlpha) {
    if (presentationTransforms_ == nullptr) {
        return;
    }
    for (auto& [_, binding] : bindings_) {
        GameObject* object = world.FindObject(binding.object);
        if (object == nullptr || !binding.hasFixedState || binding.effectiveMotionType == PhysicsMotionType::Static ||
            (runtimeActivityService_ != nullptr && !runtimeActivityService_->IsActive(binding.object))) {
            const bool removed = presentationTransforms_->Remove(binding.object);
            if (removed && object != nullptr) {
                object->MarkRenderStateDirty();
            }
            continue;
        }

        const PhysicsPose presented = binding.presentationDiscontinuity
                                          ? binding.currentFixedState.pose
                                          : InterpolatePhysicsPose(binding.previousFixedState.pose,
                                                                   binding.currentFixedState.pose, interpolationAlpha);
        PhysicsPose authoritativePose{};
        MATH::Vec3 worldScale{};
        if (!TryGetPhysicsWorldPoseAndScale(*object, authoritativePose, worldScale)) {
            if (presentationTransforms_->Remove(binding.object))
                object->MarkRenderStateDirty();
            continue;
        }
        const bool changed = presentationTransforms_->SetWorldMatrix(
            binding.object, MATH::Mat4::TRS(presented.position, presented.rotation, worldScale));
        binding.presentationDiscontinuity = false;
        if (changed)
            object->MarkRenderStateDirty();
    }
}

} // namespace HIKARI::PHYSICS
