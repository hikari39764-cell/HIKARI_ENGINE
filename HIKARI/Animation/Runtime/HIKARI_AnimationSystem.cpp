#include "Animation/Runtime/HIKARI_AnimationSystem.h"

#include <cmath>
#include <utility>
#include <unordered_set>

#include <algorithm>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "Animation/Runtime/HIKARI_AnimationNodeOverrideService.h"
#include "Animation/Runtime/HIKARI_AnimationPoseEvaluation.h"
#include "Animation/Runtime/HIKARI_AnimationPoseService.h"
#include "Animation/Runtime/HIKARI_AnimationRootMotionService.h"
#include "HIKARI/Project/FrameContext.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_RuntimeObjectActivityService.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
namespace {
float ResolveClipDuration(const ModelAsset* model, const ANIMATION::AnimationClipReference& reference) {
    if (model == nullptr || reference.IsEmpty())
        return -1.0f;
    const AnimationClip* clip = ANIMATION::ResolveAnimationClip(*model, reference);
    return clip != nullptr ? clip->durationSec : -1.0f;
}

bool SampleMotion(const ModelAsset& model, const ANIMATION::AnimationMotionSample& motion, float primaryTimeSec,
                  bool loop, ANIMATION::AnimationLocalPose& outPose, ANIMATION::AnimationLocalPose& secondaryPose,
                  ANIMATION::AnimationLocalPose& blended, float* outNormalizedTime = nullptr) {
    float normalizedTime = 0.0f;
    if (!ANIMATION::SampleAnimationClip(model, motion.primaryClip, primaryTimeSec, loop, outPose, &normalizedTime)) {
        return false;
    }
    if (outNormalizedTime != nullptr) {
        *outNormalizedTime = normalizedTime;
    }
    if (motion.secondaryClip.IsEmpty() || motion.secondaryWeight <= 0.0f) {
        return true;
    }

    const float secondaryDuration = ResolveClipDuration(&model, motion.secondaryClip);
    if (secondaryDuration <= 0.0f ||
        !ANIMATION::SampleAnimationClip(model, motion.secondaryClip, normalizedTime * secondaryDuration, loop,
                                        secondaryPose)) {
        return true;
    }
    if (ANIMATION::BlendAnimationPoses(outPose, secondaryPose, std::clamp(motion.secondaryWeight, 0.0f, 1.0f),
                                       blended)) {
        std::swap(outPose, blended);
    }
    return true;
}

void ApplyMaskedPoseLayer(const ModelAsset& model, AnimatorComponent& animator,
                          ANIMATION::AnimationLocalPose& inOutPose) {
    const AnimatorComponent::MaskedPoseLayerState& layer = animator.GetMaskedPoseLayer();
    if (!layer.active || layer.clip.IsEmpty() || layer.maskRootNodeName.empty() ||
        !std::isfinite(layer.normalizedTime) || !std::isfinite(layer.currentWeight) || layer.currentWeight <= 0.0f) {
        return;
    }

    if (!animator.PrepareMaskedPoseLayer(model)) {
        return;
    }
    ANIMATION::AnimationPoseSubtreeCache& cache = animator.GetMaskedPoseLayerCache();
    if (cache.clip == nullptr || !std::isfinite(cache.clip->durationSec) || cache.clip->durationSec <= 0.0f) {
        return;
    }

    const float sampleTime = std::clamp(layer.normalizedTime, 0.0f, 1.0f) * cache.clip->durationSec;
    if (!ANIMATION::SampleResolvedAnimationClip(model, *cache.clip, sampleTime, false, cache.overlayPose)) {
        return;
    }

    (void)ANIMATION::ApplyAnimationPoseSubtreeInPlace(model, inOutPose, cache.overlayPose, cache, layer.currentWeight,
                                                      layer.rootAlignment);
}

MATH::Quat DivideRotation(const MATH::Quat& rotation, uint32_t stepCount) noexcept {
    MATH::Quat normalized = MATH::NormalizeQ(rotation);
    if (stepCount <= 1u) {
        return normalized;
    }
    // Use the shortest quaternion arc before taking its fractional
    // power, then repeat that fixed-step rotation stepCount times.
    if (normalized.w < 0.0f) {
        normalized.x = -normalized.x;
        normalized.y = -normalized.y;
        normalized.z = -normalized.z;
        normalized.w = -normalized.w;
    }
    const float halfAngle = std::acos(std::clamp(normalized.w, -1.0f, 1.0f));
    const float sine = std::sin(halfAngle);
    if (std::abs(sine) <= 1.0e-6f) {
        // Preserve sub-microradian authored turns. Around identity,
        // the quaternion vector is linear in the half-angle, so a
        // normalized first-order split is more accurate than
        // dropping the rotation altogether.
        const float inverseSteps = 1.0f / static_cast<float>(stepCount);
        return MATH::NormalizeQ(
            {normalized.x * inverseSteps, normalized.y * inverseSteps, normalized.z * inverseSteps, 1.0f});
    }
    const float stepHalfAngle = halfAngle / static_cast<float>(stepCount);
    const float stepSine = std::sin(stepHalfAngle);
    return MATH::NormalizeQ({normalized.x / sine * stepSine, normalized.y / sine * stepSine,
                             normalized.z / sine * stepSine, std::cos(stepHalfAngle)});
}
} // namespace

void AnimationSystem::OnWorldAttached(World& world) {
    publishedObjects_.clear();
    poseService_ = world.Services().Find<ANIMATION::AnimationPoseService>();
    nodeOverrideService_ = world.Services().Find<ANIMATION::AnimationNodeOverrideService>();
    runtimeActivityService_ = world.Services().Find<RuntimeObjectActivityService>();
    if (auto* rootMotionService = world.Services().Find<ANIMATION::AnimationRootMotionService>()) {
        rootMotionService->Clear();
    }
}

void AnimationSystem::OnWorldDetached(World& world) {
    if (poseService_ != nullptr)
        poseService_->Clear();
    if (nodeOverrideService_ != nullptr)
        nodeOverrideService_->Clear();
    if (auto* rootMotionService = world.Services().Find<ANIMATION::AnimationRootMotionService>()) {
        rootMotionService->Clear();
    }
    poseService_ = nullptr;
    nodeOverrideService_ = nullptr;
    runtimeActivityService_ = nullptr;
    publishedObjects_.clear();
}

void AnimationSystem::Update(World& world, const FrameContext& frame) {
    if (poseService_ == nullptr)
        return;

    ANIMATION::AnimationRootMotionService* rootMotionService =
        world.Services().Find<ANIMATION::AnimationRootMotionService>();

    std::unordered_set<uint64_t> currentObjectKeys{};
    std::vector<RuntimeObjectHandle> currentObjects{};

    world.ForEachObjectWith<ModelComponent, AnimatorComponent>(
        WorldComponentQueryAnchor::SmallestUniqueBucket,
        [this, &frame, rootMotionService, &currentObjectKeys,
         &currentObjects](GameObject& object, ModelComponent& model, AnimatorComponent& animator) {
            const ModelAsset* modelAsset = model.GetModelAsset();
            if (modelAsset == nullptr) {
                poseService_->Remove(object.GetRuntimeHandle());
                if (rootMotionService != nullptr) {
                    rootMotionService->Remove(object.GetRuntimeHandle());
                }
                return;
            }

            const RuntimeObjectHandle objectHandle = object.GetRuntimeHandle();
            const GameProjectRuntimeActivity runtimeActivity = runtimeActivityService_ != nullptr
                                                                   ? runtimeActivityService_->Get(objectHandle)
                                                                   : GameProjectRuntimeActivity::Active;
            if (runtimeActivity == GameProjectRuntimeActivity::SuspendedHidden) {
                // hidden actor は pose residency も不要。published list
                // から外し、末尾の cleanup で一度だけ解放する。
                if (rootMotionService != nullptr) {
                    rootMotionService->Remove(objectHandle);
                }
                return;
            }
            // A suspended animator keeps its last valid pose resident so
            // rendering remains stable, but pays no clip sampling,
            // blending, masked-layer, node-override, or root-motion cost.
            // Evaluate once when no pose exists yet to avoid freezing a
            // newly spawned actor in an uninitialized pose.
            if ((runtimeActivity == GameProjectRuntimeActivity::FrozenVisible ||
                 !animator.IsRuntimeEvaluationEnabled()) &&
                poseService_->Find(objectHandle) != nullptr) {
                if (rootMotionService != nullptr) {
                    rootMotionService->Remove(objectHandle);
                }
                currentObjectKeys.insert(objectHandle.ToValue());
                currentObjects.push_back(objectHandle);
                return;
            }

            animator.BindClipToModel(*modelAsset);
            const float previousTimeSec = animator.GetTime();
            const bool previousPlaying = animator.IsPlaying();
            const bool previousFinished = animator.IsFinished();
            const bool previousTransitioning = animator.IsTransitioning();
            animator.Advance(frame.gameDt, ResolveClipDuration(modelAsset, animator.GetClipReference()));

            ANIMATION::AnimationPoseSnapshot snapshot{};
            const ANIMATION::AnimationMotionSample motion = animator.GetMotionSample();
            snapshot.primaryClip = motion.primaryClip;
            snapshot.secondaryClip = motion.secondaryClip;
            snapshot.motionBlendWeight = motion.secondaryWeight;
            snapshot.valid = SampleMotion(*modelAsset, motion, animator.GetTime(), animator.GetLoop(),
                                          snapshot.localPose, secondaryPose_, blendedPose_, &snapshot.normalizedTime);
            snapshot.transitioning = animator.IsTransitioning();
            snapshot.blendWeight = animator.GetTransitionWeight();

            if (snapshot.valid && snapshot.transitioning) {
                auto& sourcePose = transitionPose_;
                const bool sourceValid =
                    SampleMotion(*modelAsset, animator.GetTransitionSourceMotion(), animator.GetTransitionSourceTime(),
                                 animator.GetTransitionSourceLoop(), sourcePose, secondaryPose_, blendedPose_);
                if (sourceValid) {
                    if (ANIMATION::BlendAnimationPoses(sourcePose, snapshot.localPose, snapshot.blendWeight,
                                                       blendedPose_)) {
                        std::swap(snapshot.localPose, blendedPose_);
                    }
                }
            }

            if (!snapshot.valid) {
                ANIMATION::BuildBindPose(*modelAsset, snapshot.localPose);
            }
            // マスク合成前に root motion を読み取り、その後に除去する。
            // Capture は変更しないため全ボーンの base pose コピーは不要。
            snapshot.loop = animator.GetLoop();
            snapshot.playbackSpeed = animator.GetSpeed();
            snapshot.rootMotionEnabled = animator.IsRootMotionEnabled();

            // AnimationSystem runs after the fixed-step loop (order 150).
            // Queue root motion for the next fixed tick instead of
            // mutating the authoritative GameObject transform here.
            const std::shared_ptr<const ANIMATION::AnimationPoseSnapshot> previousSnapshot =
                poseService_->Find(object.GetRuntimeHandle());
            const bool rootMotionPrepared =
                snapshot.valid && snapshot.rootMotionEnabled &&
                ANIMATION::POSE_EVALUATION::CaptureRootMotionSample(*modelAsset, snapshot.localPose,
                                                                    snapshot.rootMotionSample) &&
                ANIMATION::POSE_EVALUATION::StripRootMotionFromPose(*modelAsset, snapshot.localPose);
            if (!rootMotionPrepared) {
                snapshot.rootMotionSample = {};
            }
            if (rootMotionService != nullptr && rootMotionPrepared) {
                ANIMATION::AnimationRootMotionDelta rootMotion{};
                if (ANIMATION::EvaluateAnimationRootMotion(*modelAsset, previousSnapshot.get(), snapshot, rootMotion)) {
                    const uint32_t deliverySteps = (std::max)(frame.fixedStepsThisFrame, 1u);
                    ANIMATION::AnimationRootMotionDelta step = rootMotion;
                    step.translation = rootMotion.translation * (1.0f / static_cast<float>(deliverySteps));
                    step.rotation = DivideRotation(rootMotion.rotation, deliverySteps);
                    bool queuedAny = false;
                    for (uint32_t stepIndex = 0u; stepIndex < deliverySteps; ++stepIndex) {
                        queuedAny = rootMotionService->Publish(object.GetRuntimeHandle(),
                                                               frame.fixedTickIndex + 1u + stepIndex, step) ||
                                    queuedAny;
                    }
                    if (queuedAny) {
                        snapshot.rootMotion = rootMotion;
                    }
                }
            }

            if (snapshot.valid) {
                ApplyMaskedPoseLayer(*modelAsset, animator, snapshot.localPose);
                if (nodeOverrideService_ != nullptr) {
                    (void)nodeOverrideService_->Apply(*modelAsset, object.GetRuntimeHandle(), snapshot.localPose);
                }
            }
            const bool poseChanged = poseService_->Publish(object.GetRuntimeHandle(), std::move(snapshot));
            currentObjectKeys.insert(object.GetRuntimeHandle().ToValue());
            currentObjects.push_back(object.GetRuntimeHandle());
            if (poseChanged || animator.GetTime() != previousTimeSec || animator.IsPlaying() != previousPlaying ||
                animator.IsFinished() != previousFinished || animator.IsTransitioning() != previousTransitioning) {
                object.MarkRenderStateDirty();
            }
        });

    for (const RuntimeObjectHandle object : publishedObjects_) {
        if (!currentObjectKeys.contains(object.ToValue())) {
            poseService_->Remove(object);
            if (rootMotionService != nullptr) {
                rootMotionService->Remove(object);
            }
            if (nodeOverrideService_ != nullptr) {
                nodeOverrideService_->Remove(object);
            }
        }
    }
    publishedObjects_ = std::move(currentObjects);
}

} // namespace HIKARI
