#include "HIKARI_AnimatorComponent.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI {

namespace {
float ClampTime(float value) { return (std::max)(0.0f, value); }

float ResolveDuration(const ModelAsset& model, const ANIMATION::AnimationClipReference& clip) noexcept {
    const AnimationClip* resolved = ANIMATION::ResolveAnimationClip(model, clip);
    return resolved != nullptr ? resolved->durationSec : 0.0f;
}
} // namespace

void AnimatorComponent::Serialize(nlohmann::json& out) const {
    out["clip"] = clip_.fallbackName;
    out["clipModelAssetId"] = clip_.modelAssetId.value;
    out["clipId"] = clip_.clipId.value;
    out["timeSec"] = timeSec_;
    out["speed"] = speed_;
    out["defaultBlendDurationSec"] = defaultBlendDurationSec_;
    out["loop"] = loop_;
    out["autoPlay"] = autoPlay_;
    out["rootMotionEnabled"] = rootMotionEnabled_;
    out["playing"] = playing_;
    out["finished"] = finished_;
}

void AnimatorComponent::Deserialize(const nlohmann::json& in) {
    clip_.fallbackName = in.value("clip", clip_.fallbackName);
    clip_.modelAssetId.value = in.value("clipModelAssetId", clip_.modelAssetId.value);
    clip_.clipId.value = in.value("clipId", clip_.clipId.value);
    timeSec_ = ClampTime(in.value("timeSec", timeSec_));
    speed_ = in.value("speed", speed_);
    defaultBlendDurationSec_ = (std::max)(0.0f, in.value("defaultBlendDurationSec", defaultBlendDurationSec_));
    loop_ = in.value("loop", loop_);
    autoPlay_ = in.value("autoPlay", autoPlay_);
    rootMotionEnabled_ = in.value("rootMotionEnabled", rootMotionEnabled_);
    playing_ = in.value("playing", autoPlay_);
    finished_ = in.value("finished", finished_);
    runtimeEvaluationEnabled_ = true;
    secondaryClip_ = {};
    secondaryWeight_ = 0.0f;
    ClearTransition();
    ResetMaskedPoseLayer();
}

void AnimatorComponent::Play(std::string clip, bool loop, bool restart) {
    ANIMATION::AnimationClipReference reference{};
    reference.fallbackName = std::move(clip);
    Play(std::move(reference), loop, restart);
}

void AnimatorComponent::Play(ANIMATION::AnimationClipReference clip, bool loop, bool restart) {
    ANIMATION::AnimationMotionSample motion{};
    motion.primaryClip = std::move(clip);
    PlayMotion(std::move(motion), speed_, loop, restart);
}

void AnimatorComponent::PlayMotion(ANIMATION::AnimationMotionSample motion, float speed, bool loop, bool restart) {
    clip_ = std::move(motion.primaryClip);
    secondaryClip_ = std::move(motion.secondaryClip);
    secondaryWeight_ = std::clamp(motion.secondaryWeight, 0.0f, 1.0f);
    speed_ = speed;
    loop_ = loop;
    playing_ = !clip_.IsEmpty();
    finished_ = false;
    ClearTransition();
    if (restart)
        timeSec_ = 0.0f;
}

void AnimatorComponent::CrossFade(std::string clip, float durationSeconds, bool loop, bool restart) {
    ANIMATION::AnimationClipReference reference{};
    reference.fallbackName = std::move(clip);
    CrossFade(std::move(reference), durationSeconds, loop, restart);
}

void AnimatorComponent::CrossFade(ANIMATION::AnimationClipReference clip, float durationSeconds, bool loop,
                                  bool restart) {
    ANIMATION::AnimationMotionSample motion{};
    motion.primaryClip = std::move(clip);
    CrossFadeMotion(std::move(motion), durationSeconds, speed_, loop, restart);
}

void AnimatorComponent::CrossFadeMotion(ANIMATION::AnimationMotionSample motion, float durationSeconds, float speed,
                                        bool loop, bool restart) {
    const float safeDuration = std::isfinite(durationSeconds) ? (std::max)(0.0f, durationSeconds) : 0.0f;
    if (safeDuration <= 0.0f || clip_.IsEmpty()) {
        PlayMotion(std::move(motion), speed, loop, restart);
        return;
    }
    if (motion.primaryClip == clip_ && motion.secondaryClip == secondaryClip_ && !restart) {
        secondaryWeight_ = std::clamp(motion.secondaryWeight, 0.0f, 1.0f);
        speed_ = speed;
        loop_ = loop;
        playing_ = true;
        return;
    }

    transitionSourceClip_ = clip_;
    transitionSourceSecondaryClip_ = secondaryClip_;
    transitionSourceSecondaryWeight_ = secondaryWeight_;
    transitionSourceTimeSec_ = timeSec_;
    transitionSourceSpeed_ = speed_;
    transitionSourceLoop_ = loop_;
    transitionDurationSec_ = safeDuration;
    transitionElapsedSec_ = 0.0f;

    clip_ = std::move(motion.primaryClip);
    secondaryClip_ = std::move(motion.secondaryClip);
    secondaryWeight_ = std::clamp(motion.secondaryWeight, 0.0f, 1.0f);
    speed_ = speed;
    loop_ = loop;
    playing_ = !clip_.IsEmpty();
    finished_ = false;
    if (restart)
        timeSec_ = 0.0f;
}

void AnimatorComponent::UpdateMotion(ANIMATION::AnimationMotionSample motion, const ModelAsset& model) {
    const bool primaryChanged = motion.primaryClip != clip_;
    if (primaryChanged) {
        const float oldDuration = ResolveDuration(model, clip_);
        const float newDuration = ResolveDuration(model, motion.primaryClip);
        if (oldDuration > 0.0f && newDuration > 0.0f) {
            float phase = std::fmod(timeSec_ / oldDuration, 1.0f);
            if (phase < 0.0f)
                phase += 1.0f;
            timeSec_ = phase * newDuration;
        }
    }
    clip_ = std::move(motion.primaryClip);
    secondaryClip_ = std::move(motion.secondaryClip);
    secondaryWeight_ = std::clamp(motion.secondaryWeight, 0.0f, 1.0f);
    playing_ = !clip_.IsEmpty() && !finished_;
}

void AnimatorComponent::PlayCurrent(bool restart) {
    playing_ = !clip_.IsEmpty();
    finished_ = false;
    ClearTransition();
    if (restart)
        timeSec_ = 0.0f;
}

void AnimatorComponent::PlayOnce(std::string clip, bool restart) { Play(std::move(clip), false, restart); }

void AnimatorComponent::Pause() { playing_ = false; }

void AnimatorComponent::Resume() {
    if (!clip_.IsEmpty()) {
        playing_ = true;
        finished_ = false;
    }
}

void AnimatorComponent::Stop() {
    playing_ = false;
    finished_ = true;
    timeSec_ = 0.0f;
    ClearTransition();
}

void AnimatorComponent::ResetTime() {
    timeSec_ = 0.0f;
    finished_ = false;
}

void AnimatorComponent::Advance(float deltaTimeSec, float clipDurationSec) {
    AdvanceMaskedPoseLayer(deltaTimeSec);
    if (!playing_ || clip_.IsEmpty()) {
        return;
    }

    timeSec_ += deltaTimeSec * speed_;
    if (IsTransitioning()) {
        transitionSourceTimeSec_ += deltaTimeSec * transitionSourceSpeed_;
        transitionElapsedSec_ += (std::max)(0.0f, deltaTimeSec);
        if (transitionElapsedSec_ >= transitionDurationSec_) {
            ClearTransition();
        }
    }
    if (clipDurationSec <= 0.0f) {
        timeSec_ = ClampTime(timeSec_);
        return;
    }

    if (loop_) {
        timeSec_ = std::fmod(timeSec_, clipDurationSec);
        if (timeSec_ < 0.0f) {
            timeSec_ += clipDurationSec;
        }
        finished_ = false;
        return;
    }

    if (speed_ >= 0.0f && timeSec_ >= clipDurationSec) {
        timeSec_ = clipDurationSec;
        playing_ = false;
        finished_ = true;
    } else if (speed_ < 0.0f && timeSec_ <= 0.0f) {
        timeSec_ = 0.0f;
        playing_ = false;
        finished_ = true;
    }
}

void AnimatorComponent::SetClip(std::string clip) {
    ANIMATION::AnimationClipReference reference{};
    reference.fallbackName = std::move(clip);
    SetClip(std::move(reference));
}

void AnimatorComponent::SetClip(ANIMATION::AnimationClipReference clip) {
    clip_ = std::move(clip);
    secondaryClip_ = {};
    secondaryWeight_ = 0.0f;
    finished_ = false;
    ClearTransition();
}

const ANIMATION::AnimationClipReference& AnimatorComponent::GetClipReference() const { return clip_; }

ANIMATION::AnimationMotionSample AnimatorComponent::GetMotionSample() const noexcept {
    return {clip_, secondaryClip_, secondaryWeight_};
}

void AnimatorComponent::BindClipToModel(const ModelAsset& model) {
    const auto bind = [&model](ANIMATION::AnimationClipReference& reference) {
        if (reference.fallbackName.empty())
            return;
        for (size_t index = 0u; index < model.animations.size(); ++index) {
            if (model.animations[index].name == reference.fallbackName) {
                reference = ANIMATION::MakeAnimationClipReference(model, index);
                return;
            }
        }
    };
    bind(clip_);
    bind(secondaryClip_);
    bind(transitionSourceClip_);
    bind(transitionSourceSecondaryClip_);
    bind(maskedPoseLayer_.clip);
}

void AnimatorComponent::SetTime(float timeSec) {
    timeSec_ = ClampTime(timeSec);
    finished_ = false;
}

float AnimatorComponent::GetTime() const { return timeSec_; }

void AnimatorComponent::SetSpeed(float speed) { speed_ = speed; }

float AnimatorComponent::GetSpeed() const { return speed_; }

void AnimatorComponent::SetLoop(bool loop) { loop_ = loop; }

bool AnimatorComponent::GetLoop() const { return loop_; }

void AnimatorComponent::SetRootMotionEnabled(bool enabled) noexcept { rootMotionEnabled_ = enabled; }

bool AnimatorComponent::IsRootMotionEnabled() const noexcept { return rootMotionEnabled_; }

bool AnimatorComponent::IsPlaying() const { return playing_; }

bool AnimatorComponent::IsFinished() const { return finished_; }

bool AnimatorComponent::IsFinished(float clipDurationSec) const {
    if (finished_) {
        return true;
    }
    if (clipDurationSec <= 0.0f || loop_) {
        return false;
    }
    return speed_ >= 0.0f ? timeSec_ >= clipDurationSec : timeSec_ <= 0.0f;
}

bool AnimatorComponent::IsTransitioning() const noexcept {
    return transitionDurationSec_ > 0.0f && !transitionSourceClip_.IsEmpty();
}

float AnimatorComponent::GetTransitionWeight() const noexcept {
    return IsTransitioning() ? std::clamp(transitionElapsedSec_ / transitionDurationSec_, 0.0f, 1.0f) : 1.0f;
}

ANIMATION::AnimationMotionSample AnimatorComponent::GetTransitionSourceMotion() const noexcept {
    return {transitionSourceClip_, transitionSourceSecondaryClip_, transitionSourceSecondaryWeight_};
}

float AnimatorComponent::GetTransitionSourceTime() const noexcept { return transitionSourceTimeSec_; }

bool AnimatorComponent::GetTransitionSourceLoop() const noexcept { return transitionSourceLoop_; }

void AnimatorComponent::ClearTransition() noexcept {
    transitionSourceClip_ = {};
    transitionSourceSecondaryClip_ = {};
    transitionSourceSecondaryWeight_ = 0.0f;
    transitionSourceTimeSec_ = 0.0f;
    transitionSourceSpeed_ = 1.0f;
    transitionDurationSec_ = 0.0f;
    transitionElapsedSec_ = 0.0f;
    transitionSourceLoop_ = true;
}

void AnimatorComponent::SetMaskedPoseLayer(std::string clipName, std::string maskRootNodeName, float normalizedTime,
                                           float targetWeight, float blendSeconds,
                                           ANIMATION::AnimationSubtreeRootAlignment rootAlignment) {
    SetMaskedPoseLayerView(clipName, maskRootNodeName, normalizedTime, targetWeight, blendSeconds, rootAlignment);
}

bool AnimatorComponent::SetMaskedPoseLayerResolved(std::string_view clipName, std::string_view maskRootNodeName,
                                                   float normalizedTime, float targetWeight, float blendSeconds,
                                                   const ModelAsset& model,
                                                   ANIMATION::AnimationSubtreeRootAlignment rootAlignment) {
    if (clipName.empty() || maskRootNodeName.empty() || !std::isfinite(normalizedTime) ||
        !std::isfinite(targetWeight) || !std::isfinite(blendSeconds) ||
        (rootAlignment != ANIMATION::AnimationSubtreeRootAlignment::ParentLocal &&
         rootAlignment != ANIMATION::AnimationSubtreeRootAlignment::PreserveModelRotation)) {
        return false;
    }

    const bool cacheMatches = maskedPoseLayerCache_.valid && maskedPoseLayerCache_.model == &model &&
                              maskedPoseLayerCache_.modelSourceRevision == model.sourceRevision &&
                              maskedPoseLayer_.active && maskedPoseLayer_.clip.fallbackName == clipName &&
                              maskedPoseLayer_.maskRootNodeName == maskRootNodeName &&
                              maskedPoseLayer_.rootAlignment == rootAlignment;
    ANIMATION::AnimationPoseSubtreeCache candidate{};
    if (!cacheMatches) {
        ANIMATION::AnimationClipReference clipReference{};
        clipReference.fallbackName = std::string(clipName);
        if (!ANIMATION::PrepareAnimationPoseSubtreeCache(model, maskRootNodeName, clipReference, candidate)) {
            return false;
        }
    }

    SetMaskedPoseLayerView(clipName, maskRootNodeName, normalizedTime, targetWeight, blendSeconds, rootAlignment);
    if (maskedPoseLayer_.active && !cacheMatches) {
        maskedPoseLayerCache_ = std::move(candidate);
    }
    return true;
}

void AnimatorComponent::SetMaskedPoseLayerView(std::string_view clipName, std::string_view maskRootNodeName,
                                               float normalizedTime, float targetWeight, float blendSeconds,
                                               ANIMATION::AnimationSubtreeRootAlignment rootAlignment) {
    if (clipName.empty() || maskRootNodeName.empty()) {
        ResetMaskedPoseLayer();
        return;
    }
    if (rootAlignment != ANIMATION::AnimationSubtreeRootAlignment::ParentLocal &&
        rootAlignment != ANIMATION::AnimationSubtreeRootAlignment::PreserveModelRotation) {
        ResetMaskedPoseLayer();
        return;
    }

    const float safeNormalizedTime = std::isfinite(normalizedTime) ? std::clamp(normalizedTime, 0.0f, 1.0f) : 0.0f;
    const float safeTargetWeight = std::isfinite(targetWeight) ? std::clamp(targetWeight, 0.0f, 1.0f) : 0.0f;
    const float safeBlendSeconds = std::isfinite(blendSeconds) ? (std::max)(0.0f, blendSeconds) : 0.0f;

    MaskedPoseLayerState& layer = maskedPoseLayer_;
    const bool sameLayer = layer.active && layer.clip.fallbackName == clipName &&
                           layer.maskRootNodeName == maskRootNodeName && layer.rootAlignment == rootAlignment;
    const bool targetChanged = !sameLayer || std::abs(layer.targetWeight - safeTargetWeight) > 1.0e-5f;

    if (!sameLayer) {
        layer.clip = {};
        layer.clip.fallbackName.assign(clipName.data(), clipName.size());
        layer.maskRootNodeName.assign(maskRootNodeName.data(), maskRootNodeName.size());
        layer.rootAlignment = rootAlignment;
        layer.currentWeight = 0.0f;
        layer.blendStartWeight = 0.0f;
        layer.blendElapsedSec = 0.0f;
        maskedPoseLayerCache_.Reset();
    }

    layer.active = true;
    layer.normalizedTime = safeNormalizedTime;
    if (targetChanged) {
        layer.targetWeight = safeTargetWeight;
        layer.blendStartWeight = layer.currentWeight;
        layer.blendDurationSec = safeBlendSeconds;
        layer.blendElapsedSec = 0.0f;
        if (safeBlendSeconds <= 1.0e-5f) {
            layer.currentWeight = safeTargetWeight;
            layer.blendStartWeight = safeTargetWeight;
            if (safeTargetWeight <= 0.0f) {
                ResetMaskedPoseLayer();
            }
        }
    }
}

void AnimatorComponent::ClearMaskedPoseLayer(float blendOutSeconds) {
    if (!maskedPoseLayer_.active) {
        return;
    }
    const float safeBlendSeconds = std::isfinite(blendOutSeconds) ? (std::max)(0.0f, blendOutSeconds) : 0.0f;
    if (safeBlendSeconds <= 1.0e-5f || maskedPoseLayer_.currentWeight <= 1.0e-5f) {
        ResetMaskedPoseLayer();
        return;
    }
    if (maskedPoseLayer_.targetWeight <= 1.0e-5f &&
        std::abs(maskedPoseLayer_.blendDurationSec - safeBlendSeconds) <= 1.0e-5f) {
        return;
    }
    maskedPoseLayer_.targetWeight = 0.0f;
    maskedPoseLayer_.blendStartWeight = maskedPoseLayer_.currentWeight;
    maskedPoseLayer_.blendDurationSec = safeBlendSeconds;
    maskedPoseLayer_.blendElapsedSec = 0.0f;
}

const AnimatorComponent::MaskedPoseLayerState& AnimatorComponent::GetMaskedPoseLayer() const noexcept {
    return maskedPoseLayer_;
}

bool AnimatorComponent::PrepareMaskedPoseLayer(const ModelAsset& model) {
    if (!maskedPoseLayer_.active || maskedPoseLayer_.clip.IsEmpty() || maskedPoseLayer_.maskRootNodeName.empty()) {
        return false;
    }
    if (maskedPoseLayerCache_.valid && maskedPoseLayerCache_.model == &model &&
        maskedPoseLayerCache_.modelSourceRevision == model.sourceRevision) {
        return true;
    }
    return ANIMATION::PrepareAnimationPoseSubtreeCache(model, maskedPoseLayer_.maskRootNodeName, maskedPoseLayer_.clip,
                                                       maskedPoseLayerCache_);
}

ANIMATION::AnimationPoseSubtreeCache& AnimatorComponent::GetMaskedPoseLayerCache() noexcept {
    return maskedPoseLayerCache_;
}

const ANIMATION::AnimationPoseSubtreeCache& AnimatorComponent::GetMaskedPoseLayerCache() const noexcept {
    return maskedPoseLayerCache_;
}

void AnimatorComponent::AdvanceMaskedPoseLayer(float deltaTimeSec) noexcept {
    MaskedPoseLayerState& layer = maskedPoseLayer_;
    if (!layer.active) {
        return;
    }
    if (!std::isfinite(layer.currentWeight) || !std::isfinite(layer.targetWeight) ||
        !std::isfinite(layer.blendDurationSec)) {
        ResetMaskedPoseLayer();
        return;
    }

    const float safeDelta = std::isfinite(deltaTimeSec) ? (std::max)(0.0f, deltaTimeSec) : 0.0f;
    const float safeTarget = std::clamp(layer.targetWeight, 0.0f, 1.0f);
    const float safeDuration = (std::max)(0.0f, layer.blendDurationSec);
    layer.targetWeight = safeTarget;

    if (safeDuration <= 1.0e-5f) {
        layer.currentWeight = safeTarget;
        layer.blendStartWeight = safeTarget;
    } else if (safeDelta > 0.0f && std::abs(layer.currentWeight - safeTarget) > 1.0e-5f) {
        layer.blendElapsedSec = (std::min)(safeDuration, layer.blendElapsedSec + safeDelta);
        const float amount = std::clamp(layer.blendElapsedSec / safeDuration, 0.0f, 1.0f);
        layer.currentWeight = layer.blendStartWeight + (safeTarget - layer.blendStartWeight) * amount;
    }

    layer.currentWeight = std::clamp(layer.currentWeight, 0.0f, 1.0f);
    if (safeTarget <= 1.0e-5f && layer.currentWeight <= 1.0e-5f) {
        ResetMaskedPoseLayer();
    }
}

void AnimatorComponent::ResetMaskedPoseLayer() noexcept {
    maskedPoseLayer_ = {};
    maskedPoseLayerCache_.Reset();
}

} // namespace HIKARI
