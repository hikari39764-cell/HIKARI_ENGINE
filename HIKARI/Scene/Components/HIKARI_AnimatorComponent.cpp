#include "HIKARI_AnimatorComponent.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"

namespace HIKARI {

namespace {
float ClampTime(float value) { return (std::max)(0.0f, value); }

float ResolveDuration(const ModelAsset& model, const ANIMATION::AnimationClipReference& clip) noexcept {
    const AnimationClip* resolved = ANIMATION::ResolveAnimationClip(model, clip);
    return resolved != nullptr ? resolved->durationSec : 0.0f;
}
} // namespace

AnimatorSettings NormalizeAnimatorSettings(AnimatorSettings settings) {
    return JSONFIELDS::Sanitize(std::move(settings), kAnimatorFields);
}

void AnimatorComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kAnimatorFields);
}

void AnimatorComponent::Deserialize(const nlohmann::json& in) {
    AnimatorSettings decoded{};
    JSONFIELDS::Read(in, decoded, kAnimatorFields);
    settings_ = NormalizeAnimatorSettings(std::move(decoded));
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
    PlayMotion(std::move(motion), settings_.speed, loop, restart);
}

void AnimatorComponent::PlayMotion(ANIMATION::AnimationMotionSample motion, float speed, bool loop, bool restart) {
    Clip() = std::move(motion.primaryClip);
    secondaryClip_ = std::move(motion.secondaryClip);
    secondaryWeight_ = std::clamp(motion.secondaryWeight, 0.0f, 1.0f);
    settings_.speed = speed;
    settings_.loop = loop;
    settings_.playing = !Clip().IsEmpty();
    settings_.finished = false;
    ClearTransition();
    if (restart)
        settings_.timeSec = 0.0f;
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
    CrossFadeMotion(std::move(motion), durationSeconds, settings_.speed, loop, restart);
}

void AnimatorComponent::CrossFadeMotion(ANIMATION::AnimationMotionSample motion, float durationSeconds, float speed,
                                        bool loop, bool restart) {
    const float safeDuration = std::isfinite(durationSeconds) ? (std::max)(0.0f, durationSeconds) : 0.0f;
    if (safeDuration <= 0.0f || Clip().IsEmpty()) {
        PlayMotion(std::move(motion), speed, loop, restart);
        return;
    }
    if (motion.primaryClip == Clip() && motion.secondaryClip == secondaryClip_ && !restart) {
        secondaryWeight_ = std::clamp(motion.secondaryWeight, 0.0f, 1.0f);
        settings_.speed = speed;
        settings_.loop = loop;
        settings_.playing = true;
        return;
    }

    transitionSourceClip_ = Clip();
    transitionSourceSecondaryClip_ = secondaryClip_;
    transitionSourceSecondaryWeight_ = secondaryWeight_;
    transitionSourceTimeSec_ = settings_.timeSec;
    transitionSourceSpeed_ = settings_.speed;
    transitionSourceLoop_ = settings_.loop;
    transitionDurationSec_ = safeDuration;
    transitionElapsedSec_ = 0.0f;

    Clip() = std::move(motion.primaryClip);
    secondaryClip_ = std::move(motion.secondaryClip);
    secondaryWeight_ = std::clamp(motion.secondaryWeight, 0.0f, 1.0f);
    settings_.speed = speed;
    settings_.loop = loop;
    settings_.playing = !Clip().IsEmpty();
    settings_.finished = false;
    if (restart)
        settings_.timeSec = 0.0f;
}

void AnimatorComponent::UpdateMotion(ANIMATION::AnimationMotionSample motion, const ModelAsset& model) {
    const bool primaryChanged = motion.primaryClip != Clip();
    if (primaryChanged) {
        const float oldDuration = ResolveDuration(model, Clip());
        const float newDuration = ResolveDuration(model, motion.primaryClip);
        if (oldDuration > 0.0f && newDuration > 0.0f) {
            float phase = std::fmod(settings_.timeSec / oldDuration, 1.0f);
            if (phase < 0.0f)
                phase += 1.0f;
            settings_.timeSec = phase * newDuration;
        }
    }
    Clip() = std::move(motion.primaryClip);
    secondaryClip_ = std::move(motion.secondaryClip);
    secondaryWeight_ = std::clamp(motion.secondaryWeight, 0.0f, 1.0f);
    settings_.playing = !Clip().IsEmpty() && !settings_.finished;
}

void AnimatorComponent::PlayOnce(std::string clip, bool restart) { Play(std::move(clip), false, restart); }

void AnimatorComponent::Pause() { settings_.playing = false; }

void AnimatorComponent::Resume() {
    if (!Clip().IsEmpty()) {
        settings_.playing = true;
        settings_.finished = false;
    }
}

void AnimatorComponent::Stop() {
    settings_.playing = false;
    settings_.finished = true;
    settings_.timeSec = 0.0f;
    ClearTransition();
}

void AnimatorComponent::Advance(float deltaTimeSec, float clipDurationSec) {
    AdvanceMaskedPoseLayer(deltaTimeSec);
    if (!settings_.playing || Clip().IsEmpty()) {
        return;
    }

    settings_.timeSec += deltaTimeSec * settings_.speed;
    if (IsTransitioning()) {
        transitionSourceTimeSec_ += deltaTimeSec * transitionSourceSpeed_;
        transitionElapsedSec_ += (std::max)(0.0f, deltaTimeSec);
        if (transitionElapsedSec_ >= transitionDurationSec_) {
            ClearTransition();
        }
    }
    if (clipDurationSec <= 0.0f) {
        settings_.timeSec = ClampTime(settings_.timeSec);
        return;
    }

    if (settings_.loop) {
        settings_.timeSec = std::fmod(settings_.timeSec, clipDurationSec);
        if (settings_.timeSec < 0.0f) {
            settings_.timeSec += clipDurationSec;
        }
        settings_.finished = false;
        return;
    }

    if (settings_.speed >= 0.0f && settings_.timeSec >= clipDurationSec) {
        settings_.timeSec = clipDurationSec;
        settings_.playing = false;
        settings_.finished = true;
    } else if (settings_.speed < 0.0f && settings_.timeSec <= 0.0f) {
        settings_.timeSec = 0.0f;
        settings_.playing = false;
        settings_.finished = true;
    }
}

void AnimatorComponent::SetClip(std::string clip) {
    ANIMATION::AnimationClipReference reference{};
    reference.fallbackName = std::move(clip);
    SetClip(std::move(reference));
}

void AnimatorComponent::SetClip(ANIMATION::AnimationClipReference clip) {
    Clip() = std::move(clip);
    secondaryClip_ = {};
    secondaryWeight_ = 0.0f;
    settings_.finished = false;
    ClearTransition();
}

const ANIMATION::AnimationClipReference& AnimatorComponent::GetClipReference() const { return Clip(); }

ANIMATION::AnimationMotionSample AnimatorComponent::GetMotionSample() const noexcept {
    return {Clip(), secondaryClip_, secondaryWeight_};
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
    bind(Clip());
    bind(secondaryClip_);
    bind(transitionSourceClip_);
    bind(transitionSourceSecondaryClip_);
    bind(maskedPoseLayer_.clip);
}

void AnimatorComponent::SetTime(float timeSec) {
    settings_.timeSec = ClampTime(timeSec);
    settings_.finished = false;
}

float AnimatorComponent::GetTime() const { return settings_.timeSec; }

void AnimatorComponent::SetSpeed(float speed) { settings_.speed = speed; }

float AnimatorComponent::GetSpeed() const { return settings_.speed; }

void AnimatorComponent::SetLoop(bool loop) { settings_.loop = loop; }

bool AnimatorComponent::GetLoop() const { return settings_.loop; }

void AnimatorComponent::SetRootMotionEnabled(bool enabled) noexcept { settings_.rootMotionEnabled = enabled; }

bool AnimatorComponent::IsRootMotionEnabled() const noexcept { return settings_.rootMotionEnabled; }

bool AnimatorComponent::IsPlaying() const { return settings_.playing; }

bool AnimatorComponent::IsFinished() const { return settings_.finished; }

bool AnimatorComponent::IsFinished(float clipDurationSec) const {
    if (settings_.finished) {
        return true;
    }
    if (clipDurationSec <= 0.0f || settings_.loop) {
        return false;
    }
    return settings_.speed >= 0.0f ? settings_.timeSec >= clipDurationSec : settings_.timeSec <= 0.0f;
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
