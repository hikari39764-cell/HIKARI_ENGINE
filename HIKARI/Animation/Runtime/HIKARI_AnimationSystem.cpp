#include "Animation/Runtime/HIKARI_AnimationSystem.h"

#include <utility>
#include <unordered_set>

#include <algorithm>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "Core/HIKARI_FrameContext.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
    namespace {
        float ResolveClipDuration(
            const ModelAsset* model,
            const ANIMATION::AnimationClipReference& reference) {
            if (model == nullptr || reference.IsEmpty()) return -1.0f;
            const AnimationClip* clip = ANIMATION::ResolveAnimationClip(
                *model,
                reference);
            return clip != nullptr ? clip->durationSec : -1.0f;
        }

        bool SampleMotion(
            const ModelAsset& model,
            const ANIMATION::AnimationMotionSample& motion,
            float primaryTimeSec,
            bool loop,
            ANIMATION::AnimationLocalPose& outPose,
            float* outNormalizedTime = nullptr) {
            float normalizedTime = 0.0f;
            if (!ANIMATION::SampleAnimationClip(
                    model,
                    motion.primaryClip,
                    primaryTimeSec,
                    loop,
                    outPose,
                    &normalizedTime)) {
                return false;
            }
            if (outNormalizedTime != nullptr) {
                *outNormalizedTime = normalizedTime;
            }
            if (motion.secondaryClip.IsEmpty() ||
                motion.secondaryWeight <= 0.0f) {
                return true;
            }

            const float secondaryDuration = ResolveClipDuration(
                &model, motion.secondaryClip);
            ANIMATION::AnimationLocalPose secondaryPose{};
            if (secondaryDuration <= 0.0f ||
                !ANIMATION::SampleAnimationClip(
                    model,
                    motion.secondaryClip,
                    normalizedTime * secondaryDuration,
                    loop,
                    secondaryPose)) {
                return true;
            }
            ANIMATION::AnimationLocalPose blended{};
            if (ANIMATION::BlendAnimationPoses(
                    outPose,
                    secondaryPose,
                    std::clamp(motion.secondaryWeight, 0.0f, 1.0f),
                    blended)) {
                outPose = std::move(blended);
            }
            return true;
        }
    }

    void AnimationSystem::OnWorldAttached(World& world) {
        publishedObjects_.clear();
        poseService_ = world.Services().Find<
            ANIMATION::AnimationPoseService>();
    }

    void AnimationSystem::OnWorldDetached(World&) {
        if (poseService_ != nullptr) poseService_->Clear();
        poseService_ = nullptr;
        publishedObjects_.clear();
    }

    void AnimationSystem::Update(
        World& world,
        const FrameContext& frame) {
        if (poseService_ == nullptr) return;

        std::unordered_set<uint64_t> currentObjectKeys{};
        std::vector<RuntimeObjectHandle> currentObjects{};

        world.ForEachObjectWith<ModelComponent, AnimatorComponent>(
            [this, &frame, &currentObjectKeys, &currentObjects](
                GameObject& object,
                ModelComponent& model,
                AnimatorComponent& animator) {
                const ModelAsset* modelAsset = model.GetModelAsset();
                if (modelAsset == nullptr) {
                    poseService_->Remove(object.GetRuntimeHandle());
                    return;
                }

                animator.BindClipToModel(*modelAsset);
                const float previousTimeSec = animator.GetTime();
                const bool previousPlaying = animator.IsPlaying();
                const bool previousFinished = animator.IsFinished();
                const bool previousTransitioning =
                    animator.IsTransitioning();
                animator.Advance(
                    frame.gameDt,
                    ResolveClipDuration(
                        modelAsset,
                        animator.GetClipReference()));

                ANIMATION::AnimationPoseSnapshot snapshot{};
                const ANIMATION::AnimationMotionSample motion =
                    animator.GetMotionSample();
                snapshot.primaryClip = motion.primaryClip;
                snapshot.secondaryClip = motion.secondaryClip;
                snapshot.motionBlendWeight = motion.secondaryWeight;
                snapshot.valid = SampleMotion(
                    *modelAsset,
                    motion,
                    animator.GetTime(),
                    animator.GetLoop(),
                    snapshot.localPose,
                    &snapshot.normalizedTime);
                snapshot.transitioning = animator.IsTransitioning();
                snapshot.blendWeight = animator.GetTransitionWeight();

                if (snapshot.valid && snapshot.transitioning) {
                    ANIMATION::AnimationLocalPose sourcePose{};
                    const bool sourceValid = SampleMotion(
                            *modelAsset,
                            animator.GetTransitionSourceMotion(),
                            animator.GetTransitionSourceTime(),
                            animator.GetTransitionSourceLoop(),
                            sourcePose);
                    if (sourceValid) {
                        ANIMATION::AnimationLocalPose blended{};
                        if (ANIMATION::BlendAnimationPoses(
                                sourcePose,
                                snapshot.localPose,
                                snapshot.blendWeight,
                                blended)) {
                            snapshot.localPose = std::move(blended);
                        }
                    }
                }

                if (!snapshot.valid) {
                    ANIMATION::BuildBindPose(
                        *modelAsset,
                        snapshot.localPose);
                }
                const bool poseChanged = poseService_->Publish(
                    object.GetRuntimeHandle(),
                    std::move(snapshot));
                currentObjectKeys.insert(
                    object.GetRuntimeHandle().ToValue());
                currentObjects.push_back(object.GetRuntimeHandle());
                if (poseChanged ||
                    animator.GetTime() != previousTimeSec ||
                    animator.IsPlaying() != previousPlaying ||
                    animator.IsFinished() != previousFinished ||
                    animator.IsTransitioning() !=
                        previousTransitioning) {
                    object.MarkRenderStateDirty();
                }
            });

        for (const RuntimeObjectHandle object : publishedObjects_) {
            if (!currentObjectKeys.contains(object.ToValue())) {
                poseService_->Remove(object);
            }
        }
        publishedObjects_ = std::move(currentObjects);
    }

} // namespace HIKARI
