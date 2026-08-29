#pragma once

#include <string>
#include <string_view>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
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

        void SetRootMotionEnabled(bool enabled) noexcept;
        bool IsRootMotionEnabled() const noexcept;

        // Runtime-only animation LOD gate. Disabled animators retain their
        // last published pose without advancing or resampling it. This state
        // is never serialized into authored Scene data.
        void SetRuntimeEvaluationEnabled(bool enabled) noexcept {
            runtimeEvaluationEnabled_ = enabled;
        }
        bool IsRuntimeEvaluationEnabled() const noexcept {
            return runtimeEvaluationEnabled_;
        }

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

        struct MaskedPoseLayerState {
            ANIMATION::AnimationClipReference clip{};
            std::string maskRootNodeName{};
            float normalizedTime = 0.0f;
            float currentWeight = 0.0f;
            float targetWeight = 0.0f;
            float blendStartWeight = 0.0f;
            float blendDurationSec = 0.0f;
            float blendElapsedSec = 0.0f;
            ANIMATION::AnimationSubtreeRootAlignment rootAlignment =
                ANIMATION::AnimationSubtreeRootAlignment::ParentLocal;
            bool active = false;
        };

        // The game-facing bridge uses these narrow methods to drive one
        // transient upper-body (or other subtree) overlay.  The base
        // Animator timeline remains independent and may continue advancing.
        void SetMaskedPoseLayer(
            std::string clipName,
            std::string maskRootNodeName,
            float normalizedTime,
            float targetWeight,
            float blendSeconds,
            ANIMATION::AnimationSubtreeRootAlignment rootAlignment =
                ANIMATION::AnimationSubtreeRootAlignment::ParentLocal);

        // Resolved bridge path.  The model is supplied by the owning
        // ModelComponent so clip/root validation and subtree topology are
        // cached once instead of repeating string scans in every frame.
        bool SetMaskedPoseLayerResolved(
            std::string_view clipName,
            std::string_view maskRootNodeName,
            float normalizedTime,
            float targetWeight,
            float blendSeconds,
            const ModelAsset& model,
            ANIMATION::AnimationSubtreeRootAlignment rootAlignment =
                ANIMATION::AnimationSubtreeRootAlignment::ParentLocal);
        void ClearMaskedPoseLayer(float blendOutSeconds = 0.0f);
        const MaskedPoseLayerState& GetMaskedPoseLayer() const noexcept;

        // AnimationSystem uses the resolved cache to sample and blend the
        // overlay without allocating full-pose temporaries each frame.
        bool PrepareMaskedPoseLayer(const ModelAsset& model);
        ANIMATION::AnimationPoseSubtreeCache&
            GetMaskedPoseLayerCache() noexcept;
        const ANIMATION::AnimationPoseSubtreeCache&
            GetMaskedPoseLayerCache() const noexcept;

    private:
        void ClearTransition() noexcept;
        void SetMaskedPoseLayerView(
            std::string_view clipName,
            std::string_view maskRootNodeName,
            float normalizedTime,
            float targetWeight,
            float blendSeconds,
            ANIMATION::AnimationSubtreeRootAlignment rootAlignment);
        void AdvanceMaskedPoseLayer(float deltaTimeSec) noexcept;
        void ResetMaskedPoseLayer() noexcept;

        ANIMATION::AnimationClipReference clip_{};
        ANIMATION::AnimationClipReference secondaryClip_{};
        float secondaryWeight_ = 0.0f;
        float timeSec_ = 0.0f;
        float speed_ = 1.0f;
        float defaultBlendDurationSec_ = 0.2f;
        bool loop_ = true;
        bool autoPlay_ = true;
        bool rootMotionEnabled_ = false;
        bool runtimeEvaluationEnabled_ = true;
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

        MaskedPoseLayerState maskedPoseLayer_{};
        ANIMATION::AnimationPoseSubtreeCache maskedPoseLayerCache_{};
    };

} // namespace HIKARI
