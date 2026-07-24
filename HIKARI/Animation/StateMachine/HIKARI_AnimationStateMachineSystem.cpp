#include "Animation/StateMachine/HIKARI_AnimationStateMachineSystem.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "Animation/StateMachine/HIKARI_AnimationStateMachineRuntimeService.h"
#include "Animation/StateMachine/HIKARI_CharacterMotionAnimationBridge.h"
#include "Assets/Animation/HIKARI_AnimationStateMachineAssetStore.h"
#include "Gameplay/Motion/HIKARI_CharacterMotionStateService.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Scene/Components/HIKARI_AnimationStateMachineComponent.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
    namespace {
        float ResolveNormalizedTime(
            const ModelAsset* model,
            const AnimatorComponent& animator) noexcept {
            if (model == nullptr) return 0.0f;
            const AnimationClip* clip = ANIMATION::ResolveAnimationClip(
                *model,
                animator.GetClipReference());
            if (clip == nullptr || clip->durationSec <= 0.0f) return 0.0f;
            return std::clamp(
                animator.GetTime() / clip->durationSec,
                0.0f,
                1.0f);
        }

        void PlayState(
            AnimatorComponent& animator,
            const ANIMATION::AnimationState& state,
            const ANIMATION::AnimationStateMotionEvaluation& motion,
            float blendDurationSec,
            bool initial) {
            if (initial || blendDurationSec <= 0.0f) {
                animator.PlayMotion(
                    motion.sample,
                    state.speed * motion.speedScale,
                    state.loop,
                    true);
            } else {
                animator.CrossFadeMotion(
                    motion.sample,
                    blendDurationSec,
                    state.speed * motion.speedScale,
                    state.loop,
                    true);
            }
        }
    }

    void AnimationStateMachineSystem::OnWorldAttached(World& world) {
        assetStore_ = world.Services().Find<
            AnimationStateMachineAssetStore>();
        runtimeService_ = world.Services().Find<
            ANIMATION::AnimationStateMachineRuntimeService>();
        motionStateService_ = world.Services().Find<
            GAMEPLAY::CharacterMotionStateService>();
        activeObjects_.clear();
    }

    void AnimationStateMachineSystem::OnWorldDetached(World&) {
        if (runtimeService_ != nullptr) runtimeService_->Clear();
        assetStore_ = nullptr;
        runtimeService_ = nullptr;
        motionStateService_ = nullptr;
        activeObjects_.clear();
    }

    void AnimationStateMachineSystem::Update(
        World& world,
        const FrameContext&) {
        if (assetStore_ == nullptr || runtimeService_ == nullptr) return;
        std::unordered_set<uint64_t> currentKeys{};
        std::vector<RuntimeObjectHandle> currentObjects{};

        world.ForEachObjectWith<
            AnimationStateMachineComponent,
            AnimatorComponent>(
            [this, &currentKeys, &currentObjects](
                GameObject& object,
                AnimationStateMachineComponent& controller,
                AnimatorComponent& animator) {
                const RuntimeObjectHandle handle = object.GetRuntimeHandle();
                currentKeys.insert(handle.ToValue());
                currentObjects.push_back(handle);
                if (!controller.IsEnabled() ||
                    controller.GetAssetGuid().empty()) {
                    runtimeService_->Remove(handle);
                    return;
                }

                ANIMATION::AnimationStateMachineInstance& instance =
                    runtimeService_->Acquire(handle);
                std::string loadError{};
                const auto asset = assetStore_->Load(
                    AssetGuid{ controller.GetAssetGuid() },
                    &loadError);
                if (!asset) {
                    instance.SetError(std::move(loadError));
                    return;
                }
                const bool rebound = instance.Bind(asset);
                if (rebound && controller.GetPlayOnStart()) {
                    (void)instance.Start(true);
                }

                const auto playbackRequest =
                    instance.ConsumePlaybackRequest();
                if (playbackRequest == ANIMATION::
                        AnimationStateMachinePlaybackRequest::Start) {
                    if (const ANIMATION::AnimationState* state =
                            instance.GetCurrentState()) {
                        const auto motion = instance.ResolveCurrentMotion();
                        if (motion.valid) {
                            PlayState(
                                animator, *state, motion, 0.0f, true);
                        }
                    }
                } else if (playbackRequest == ANIMATION::
                        AnimationStateMachinePlaybackRequest::Stop) {
                    animator.Stop();
                }

                if (!instance.GetSnapshot().running) return;

                if (controller.GetSyncCharacterMotion() &&
                    motionStateService_ != nullptr) {
                    const GAMEPLAY::CharacterMotionState* motion =
                        motionStateService_->Find(handle);
                    if (motion != nullptr) {
                        ANIMATION::ApplyCharacterMotionAnimationParameters(
                            instance,
                            *motion);
                    }
                }

                const ModelComponent* model =
                    object.GetComponent<ModelComponent>();
                const ModelAsset* modelAsset = model != nullptr
                    ? model->GetModelAsset()
                    : nullptr;
                if (const ANIMATION::AnimationState* state =
                        instance.GetCurrentState()) {
                    const auto motion = instance.ResolveCurrentMotion();
                    if (motion.valid && modelAsset != nullptr) {
                        animator.UpdateMotion(motion.sample, *modelAsset);
                        animator.SetSpeed(
                            state->speed * motion.speedScale);
                        animator.SetLoop(state->loop);
                    }
                }
                const float normalizedTime = ResolveNormalizedTime(
                    modelAsset,
                    animator);
                const ANIMATION::AnimationStateTransition* transition =
                    instance.Evaluate(
                        normalizedTime,
                        animator.IsFinished());
                if (transition != nullptr &&
                    instance.CommitTransition(transition->targetStateId)) {
                    if (const ANIMATION::AnimationState* target =
                            instance.GetCurrentState()) {
                        const auto motion = instance.ResolveCurrentMotion();
                        if (motion.valid) {
                            PlayState(
                                animator,
                                *target,
                                motion,
                                transition->blendDurationSec,
                                false);
                        }
                    }
                }
                instance.ClearTriggers();
            });

        for (const RuntimeObjectHandle object : activeObjects_) {
            if (!currentKeys.contains(object.ToValue())) {
                runtimeService_->Remove(object);
            }
        }
        activeObjects_ = std::move(currentObjects);
    }

} // namespace HIKARI
