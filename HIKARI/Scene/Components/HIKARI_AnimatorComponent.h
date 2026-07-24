#pragma once

#include <string>
#include <string_view>

#include "Animation/Runtime/HIKARI_AnimationPose.h"
#include "HIKARI_IComponent.h"

namespace HIKARI { class ModelAsset; }

namespace HIKARI {

    class AnimatorComponent final : public IComponent {
    public:
        std::string_view GetTypeName() const override { return "AnimatorComponent"; }

        void Serialize(nlohmann::json& out) const override;
        void Deserialize(const nlohmann::json& in) override;
        void BuildInspector(IInspectorBuilder& builder) override;
        void RenderImGui() override;

        void Play(std::string clip, bool loop = true, bool restart = true);
        void Play(
            ANIMATION::AnimationClipReference clip,
            bool loop = true,
            bool restart = true);
        void PlayMotion(
            ANIMATION::AnimationMotionSample motion,
            float speed = 1.0f,
            bool loop = true,
            bool restart = true);
        void CrossFade(
            std::string clip,
            float durationSeconds,
            bool loop = true,
            bool restart = true);
        void CrossFade(
            ANIMATION::AnimationClipReference clip,
            float durationSeconds,
            bool loop = true,
            bool restart = true);
        void CrossFadeMotion(
            ANIMATION::AnimationMotionSample motion,
            float durationSeconds,
            float speed = 1.0f,
            bool loop = true,
            bool restart = true);
        void UpdateMotion(
            ANIMATION::AnimationMotionSample motion,
            const ModelAsset& model);
        void PlayCurrent(bool restart = true);
        void PlayOnce(std::string clip, bool restart = true);
        void Pause();
        void Resume();
        void Stop();
        void ResetTime();
        void Advance(float deltaTimeSec, float clipDurationSec = -1.0f);

        void SetClip(std::string clip);
        void SetClip(ANIMATION::AnimationClipReference clip);
        const std::string& GetClip() const;
        const ANIMATION::AnimationClipReference& GetClipReference() const;
        ANIMATION::AnimationMotionSample GetMotionSample() const noexcept;
        void BindClipToModel(const ModelAsset& model);

        void SetTime(float timeSec);
        float GetTime() const;

        void SetSpeed(float speed);
        float GetSpeed() const;

        void SetLoop(bool loop);
        bool GetLoop() const;

        void SetAutoPlay(bool autoPlay);
        bool GetAutoPlay() const;

        void SetPlaying(bool playing);
        bool IsPlaying() const;

        bool IsFinished() const;
        bool IsFinished(float clipDurationSec) const;

        void SetDefaultBlendDuration(float durationSeconds);
        float GetDefaultBlendDuration() const noexcept;
        bool IsTransitioning() const noexcept;
        float GetTransitionWeight() const noexcept;
        const ANIMATION::AnimationClipReference&
            GetTransitionSourceClip() const noexcept;
        ANIMATION::AnimationMotionSample
            GetTransitionSourceMotion() const noexcept;
        float GetTransitionSourceTime() const noexcept;
        float GetTransitionSourceSpeed() const noexcept;
        bool GetTransitionSourceLoop() const noexcept;

    private:
        void ClearTransition() noexcept;

        ANIMATION::AnimationClipReference clip_{};
        ANIMATION::AnimationClipReference secondaryClip_{};
        float secondaryWeight_ = 0.0f;
        float timeSec_ = 0.0f;
        float speed_ = 1.0f;
        float defaultBlendDurationSec_ = 0.2f;
        bool loop_ = true;
        bool autoPlay_ = true;
        bool playing_ = true;
        bool finished_ = false;

        ANIMATION::AnimationClipReference transitionSourceClip_{};
        ANIMATION::AnimationClipReference transitionSourceSecondaryClip_{};
        float transitionSourceSecondaryWeight_ = 0.0f;
        float transitionSourceTimeSec_ = 0.0f;
        float transitionSourceSpeed_ = 1.0f;
        float transitionDurationSec_ = 0.0f;
        float transitionElapsedSec_ = 0.0f;
        bool transitionSourceLoop_ = true;
    };

} // namespace HIKARI
