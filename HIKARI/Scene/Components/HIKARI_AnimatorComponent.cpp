#include "HIKARI_AnimatorComponent.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"

#if defined(HIKARI_ENABLE_IMGUI)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
        float ClampTime(float value) {
            return (std::max)(0.0f, value);
        }

        const ModelAsset* FindOwnerModelAsset(const AnimatorComponent& animator) {
            const GameObject* owner = animator.GetOwner();
            if (owner == nullptr) {
                return nullptr;
            }
            const ModelComponent* model = owner->GetComponent<ModelComponent>();
            return model != nullptr ? model->GetModelAsset() : nullptr;
        }

        float ResolveDuration(
            const ModelAsset& model,
            const ANIMATION::AnimationClipReference& clip) noexcept {
            const AnimationClip* resolved =
                ANIMATION::ResolveAnimationClip(model, clip);
            return resolved != nullptr ? resolved->durationSec : 0.0f;
        }

#if defined(HIKARI_ENABLE_IMGUI)
        const AnimationClip* FindClip(
            const ModelAsset* asset,
            const ANIMATION::AnimationClipReference& reference) {
            return asset != nullptr
                ? ANIMATION::ResolveAnimationClip(*asset, reference)
                : nullptr;
        }
#endif
    }

    void AnimatorComponent::Serialize(nlohmann::json& out) const {
        out["clip"] = clip_.fallbackName;
        out["clipModelAssetId"] = clip_.modelAssetId.value;
        out["clipId"] = clip_.clipId.value;
        out["timeSec"] = timeSec_;
        out["speed"] = speed_;
        out["defaultBlendDurationSec"] = defaultBlendDurationSec_;
        out["loop"] = loop_;
        out["autoPlay"] = autoPlay_;
        out["playing"] = playing_;
        out["finished"] = finished_;
    }

    void AnimatorComponent::Deserialize(const nlohmann::json& in) {
        clip_.fallbackName = in.value(
            "clip", clip_.fallbackName);
        clip_.modelAssetId.value = in.value(
            "clipModelAssetId", clip_.modelAssetId.value);
        clip_.clipId.value = in.value(
            "clipId", clip_.clipId.value);
        timeSec_ = ClampTime(in.value("timeSec", timeSec_));
        speed_ = in.value("speed", speed_);
        defaultBlendDurationSec_ = (std::max)(
            0.0f,
            in.value(
                "defaultBlendDurationSec",
                defaultBlendDurationSec_));
        loop_ = in.value("loop", loop_);
        autoPlay_ = in.value("autoPlay", autoPlay_);
        playing_ = in.value("playing", autoPlay_);
        finished_ = in.value("finished", finished_);
        secondaryClip_ = {};
        secondaryWeight_ = 0.0f;
        ClearTransition();
    }

    void AnimatorComponent::BuildInspector(IInspectorBuilder& builder) {
        if (builder.String("Clip", clip_.fallbackName)) {
            // A hand-edited name is no longer guaranteed to describe the
            // previously resolved asset/clip pair. The AnimationSystem will
            // resolve and bind the new name on its next update.
            clip_.modelAssetId = {};
            clip_.clipId = {};
            ClearTransition();
        }
        builder.Float("Time Sec", timeSec_);
        builder.Float("Speed", speed_);
        builder.Float("Default Blend Sec", defaultBlendDurationSec_);
        builder.Bool("Loop", loop_);
        builder.Bool("Auto Play", autoPlay_);
        builder.Bool("Playing", playing_);
        builder.Bool("Finished", finished_);
        timeSec_ = ClampTime(timeSec_);
        defaultBlendDurationSec_ = (std::max)(
            0.0f,
            defaultBlendDurationSec_);
    }

    void AnimatorComponent::RenderImGui() {
#if defined(HIKARI_ENABLE_IMGUI)
        const ModelAsset* modelAsset = FindOwnerModelAsset(*this);
        const AnimationClip* currentClip = FindClip(modelAsset, clip_);
        int currentIndex = -1;
        if (modelAsset != nullptr) {
            for (size_t i = 0; i < modelAsset->animations.size(); ++i) {
                if (modelAsset->GetAnimationClipId(i) == clip_.clipId ||
                    modelAsset->animations[i].name == clip_.fallbackName) {
                    currentIndex = static_cast<int>(i);
                    currentClip = &modelAsset->animations[i];
                    break;
                }
            }
        }

        const char* preview = clip_.fallbackName.empty()
            ? "<none>"
            : clip_.fallbackName.c_str();
        if (modelAsset != nullptr && !modelAsset->animations.empty()) {
            if (ImGui::BeginCombo("Clip From Model", preview)) {
                for (size_t i = 0; i < modelAsset->animations.size(); ++i) {
                    const AnimationClip& clip = modelAsset->animations[i];
                    const bool selected = static_cast<int>(i) == currentIndex;
                    std::string label = clip.name + " (" + std::to_string(clip.durationSec) + "s)";
                    if (ImGui::Selectable(label.c_str(), selected)) {
                        CrossFade(
                            ANIMATION::MakeAnimationClipReference(
                                *modelAsset,
                                i),
                            defaultBlendDurationSec_,
                            loop_,
                            true);
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        } else {
            ImGui::TextUnformatted("Animation Clip: <no clips on current model>");
        }

        if (currentClip != nullptr) {
            const float duration = currentClip->durationSec;
            const float normalized = duration > 0.0f ? std::clamp(timeSec_ / duration, 0.0f, 1.0f) : 0.0f;
            ImGui::Text("Current Clip Duration: %.2f", duration);
            ImGui::Text("Progress: %.2f / %.2f", timeSec_, duration);
            ImGui::Text("Normalized Time: %.3f", normalized);
            ImGui::ProgressBar(normalized);
        } else if (!clip_.fallbackName.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Clip not found in current model");
        }

        if (IsTransitioning()) {
            ImGui::Text(
                "Blend: %.0f%%",
                GetTransitionWeight() * 100.0f);
            ImGui::ProgressBar(GetTransitionWeight());
        }

        if (ImGui::Button("Play")) {
            PlayCurrent(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Pause")) {
            Pause();
        }
        ImGui::SameLine();
        if (ImGui::Button("Resume")) {
            Resume();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop")) {
            Stop();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Time")) {
            ResetTime();
        }
        ImGui::SameLine();
        if (ImGui::Button("Play Once")) {
            PlayOnce(clip_.fallbackName, true);
        }
#endif
    }

    void AnimatorComponent::Play(std::string clip, bool loop, bool restart) {
        ANIMATION::AnimationClipReference reference{};
        reference.fallbackName = std::move(clip);
        Play(std::move(reference), loop, restart);
    }

    void AnimatorComponent::Play(
        ANIMATION::AnimationClipReference clip,
        bool loop,
        bool restart) {
        ANIMATION::AnimationMotionSample motion{};
        motion.primaryClip = std::move(clip);
        PlayMotion(std::move(motion), speed_, loop, restart);
    }

    void AnimatorComponent::PlayMotion(
        ANIMATION::AnimationMotionSample motion,
        float speed,
        bool loop,
        bool restart) {
        clip_ = std::move(motion.primaryClip);
        secondaryClip_ = std::move(motion.secondaryClip);
        secondaryWeight_ = std::clamp(
            motion.secondaryWeight, 0.0f, 1.0f);
        speed_ = speed;
        loop_ = loop;
        playing_ = !clip_.IsEmpty();
        finished_ = false;
        ClearTransition();
        if (restart) timeSec_ = 0.0f;
    }

    void AnimatorComponent::CrossFade(
        std::string clip,
        float durationSeconds,
        bool loop,
        bool restart) {
        ANIMATION::AnimationClipReference reference{};
        reference.fallbackName = std::move(clip);
        CrossFade(
            std::move(reference),
            durationSeconds,
            loop,
            restart);
    }

    void AnimatorComponent::CrossFade(
        ANIMATION::AnimationClipReference clip,
        float durationSeconds,
        bool loop,
        bool restart) {
        ANIMATION::AnimationMotionSample motion{};
        motion.primaryClip = std::move(clip);
        CrossFadeMotion(
            std::move(motion),
            durationSeconds,
            speed_,
            loop,
            restart);
    }

    void AnimatorComponent::CrossFadeMotion(
        ANIMATION::AnimationMotionSample motion,
        float durationSeconds,
        float speed,
        bool loop,
        bool restart) {
        const float safeDuration = std::isfinite(durationSeconds)
            ? (std::max)(0.0f, durationSeconds)
            : 0.0f;
        if (safeDuration <= 0.0f || clip_.IsEmpty()) {
            PlayMotion(std::move(motion), speed, loop, restart);
            return;
        }
        if (motion.primaryClip == clip_ &&
            motion.secondaryClip == secondaryClip_ && !restart) {
            secondaryWeight_ = std::clamp(
                motion.secondaryWeight, 0.0f, 1.0f);
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
        secondaryWeight_ = std::clamp(
            motion.secondaryWeight, 0.0f, 1.0f);
        speed_ = speed;
        loop_ = loop;
        playing_ = !clip_.IsEmpty();
        finished_ = false;
        if (restart) timeSec_ = 0.0f;
    }

    void AnimatorComponent::UpdateMotion(
        ANIMATION::AnimationMotionSample motion,
        const ModelAsset& model) {
        const bool primaryChanged = motion.primaryClip != clip_;
        if (primaryChanged) {
            const float oldDuration = ResolveDuration(model, clip_);
            const float newDuration = ResolveDuration(
                model, motion.primaryClip);
            if (oldDuration > 0.0f && newDuration > 0.0f) {
                float phase = std::fmod(timeSec_ / oldDuration, 1.0f);
                if (phase < 0.0f) phase += 1.0f;
                timeSec_ = phase * newDuration;
            }
        }
        clip_ = std::move(motion.primaryClip);
        secondaryClip_ = std::move(motion.secondaryClip);
        secondaryWeight_ = std::clamp(
            motion.secondaryWeight, 0.0f, 1.0f);
        playing_ = !clip_.IsEmpty() && !finished_;
    }

    void AnimatorComponent::PlayCurrent(bool restart) {
        playing_ = !clip_.IsEmpty();
        finished_ = false;
        ClearTransition();
        if (restart) timeSec_ = 0.0f;
    }

    void AnimatorComponent::PlayOnce(std::string clip, bool restart) {
        Play(std::move(clip), false, restart);
    }

    void AnimatorComponent::Pause() {
        playing_ = false;
    }

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
        if (!playing_ || clip_.IsEmpty()) {
            return;
        }

        timeSec_ += deltaTimeSec * speed_;
        if (IsTransitioning()) {
            transitionSourceTimeSec_ +=
                deltaTimeSec * transitionSourceSpeed_;
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

    void AnimatorComponent::SetClip(
        ANIMATION::AnimationClipReference clip) {
        clip_ = std::move(clip);
        secondaryClip_ = {};
        secondaryWeight_ = 0.0f;
        finished_ = false;
        ClearTransition();
    }

    const std::string& AnimatorComponent::GetClip() const {
        return clip_.fallbackName;
    }

    const ANIMATION::AnimationClipReference&
        AnimatorComponent::GetClipReference() const {
        return clip_;
    }

    ANIMATION::AnimationMotionSample
        AnimatorComponent::GetMotionSample() const noexcept {
        return { clip_, secondaryClip_, secondaryWeight_ };
    }

    void AnimatorComponent::BindClipToModel(const ModelAsset& model) {
        const auto bind = [&model](
            ANIMATION::AnimationClipReference& reference) {
            if (reference.fallbackName.empty()) return;
            for (size_t index = 0u;
                    index < model.animations.size();
                    ++index) {
                if (model.animations[index].name ==
                        reference.fallbackName) {
                    reference = ANIMATION::MakeAnimationClipReference(
                        model,
                        index);
                    return;
                }
            }
        };
        bind(clip_);
        bind(secondaryClip_);
        bind(transitionSourceClip_);
        bind(transitionSourceSecondaryClip_);
    }

    void AnimatorComponent::SetTime(float timeSec) {
        timeSec_ = ClampTime(timeSec);
        finished_ = false;
    }

    float AnimatorComponent::GetTime() const {
        return timeSec_;
    }

    void AnimatorComponent::SetSpeed(float speed) {
        speed_ = speed;
    }

    float AnimatorComponent::GetSpeed() const {
        return speed_;
    }

    void AnimatorComponent::SetLoop(bool loop) {
        loop_ = loop;
    }

    bool AnimatorComponent::GetLoop() const {
        return loop_;
    }

    void AnimatorComponent::SetAutoPlay(bool autoPlay) {
        autoPlay_ = autoPlay;
    }

    bool AnimatorComponent::GetAutoPlay() const {
        return autoPlay_;
    }

    void AnimatorComponent::SetPlaying(bool playing) {
        playing_ = playing;
        if (playing_) {
            finished_ = false;
        }
    }

    bool AnimatorComponent::IsPlaying() const {
        return playing_;
    }

    bool AnimatorComponent::IsFinished() const {
        return finished_;
    }

    bool AnimatorComponent::IsFinished(float clipDurationSec) const {
        if (finished_) {
            return true;
        }
        if (clipDurationSec <= 0.0f || loop_) {
            return false;
        }
        return speed_ >= 0.0f ? timeSec_ >= clipDurationSec : timeSec_ <= 0.0f;
    }

    void AnimatorComponent::SetDefaultBlendDuration(
        float durationSeconds) {
        defaultBlendDurationSec_ = std::isfinite(durationSeconds)
            ? (std::max)(0.0f, durationSeconds)
            : 0.0f;
    }

    float AnimatorComponent::GetDefaultBlendDuration() const noexcept {
        return defaultBlendDurationSec_;
    }

    bool AnimatorComponent::IsTransitioning() const noexcept {
        return transitionDurationSec_ > 0.0f &&
            !transitionSourceClip_.IsEmpty();
    }

    float AnimatorComponent::GetTransitionWeight() const noexcept {
        return IsTransitioning()
            ? std::clamp(
                transitionElapsedSec_ / transitionDurationSec_,
                0.0f,
                1.0f)
            : 1.0f;
    }

    const ANIMATION::AnimationClipReference&
        AnimatorComponent::GetTransitionSourceClip() const noexcept {
        return transitionSourceClip_;
    }

    ANIMATION::AnimationMotionSample
        AnimatorComponent::GetTransitionSourceMotion() const noexcept {
        return {
            transitionSourceClip_,
            transitionSourceSecondaryClip_,
            transitionSourceSecondaryWeight_
        };
    }

    float AnimatorComponent::GetTransitionSourceTime() const noexcept {
        return transitionSourceTimeSec_;
    }

    float AnimatorComponent::GetTransitionSourceSpeed() const noexcept {
        return transitionSourceSpeed_;
    }

    bool AnimatorComponent::GetTransitionSourceLoop() const noexcept {
        return transitionSourceLoop_;
    }

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

} // namespace HIKARI
