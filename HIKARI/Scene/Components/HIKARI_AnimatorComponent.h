#pragma once

#include <string>
#include <string_view>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "HIKARI_IComponent.h"

namespace HIKARI {
class ModelAsset;
}

namespace HIKARI {

// 作者値（シーン文書の properties と同じ形）。項目表（kAnimatorFields）が保存・読み取り・検査器・説明を決める。クリップの
// 指定（名前・モデル資産・クリップ ID）は ANIMATION::AnimationClipReference そのもので、部件は再生の入口からこれを書き換える。
// 再生中の状態（時刻・再生中・終了）も保存する。
struct AnimatorSettings : ANIMATION::AnimationClipReference {
    float timeSec = 0.0f;
    float speed = 1.0f;
    bool loop = true;
    bool rootMotionEnabled = false;
    bool playing = true;
    bool finished = false;
};

inline constexpr auto kAnimatorFields = [](auto&& field) {
    using S = AnimatorSettings;
    using R = ANIMATION::AnimationClipReference;
    using namespace JSONFIELDS;
    field("clip", &R::fallbackName, Label{"Clip"});
    // クリップの指定を解決した結果。名前を手で替えた時は捨てる（AnimationSystem が次の更新で解決し直す）。
    field("clipModelAssetId", &R::modelAssetId, Label{"Clip Model Asset"}, Hidden{});
    field("clipId", &R::clipId, Label{"Clip ID"}, Hidden{});
    field("timeSec", &S::timeSec, Range{0.0, 3600.0}, Label{"Time Sec"});
    field("speed", &S::speed, Label{"Speed"});
    field("loop", &S::loop, Label{"Loop"});
    field("rootMotionEnabled", &S::rootMotionEnabled, Label{"Root Motion"},
          Help{"Remove the root bone's movement from the pose and publish it for character movement or project code "
               "to apply."});
    field("playing", &S::playing, Label{"Playing"});
    field("finished", &S::finished, Label{"Finished"});
};

// 範囲へ丸める。
AnimatorSettings NormalizeAnimatorSettings(AnimatorSettings settings);

class AnimatorComponent final : public IComponent {
public:
    std::string_view GetTypeName() const override { return "AnimatorComponent"; }

    void Serialize(nlohmann::json& out) const override;
    void Deserialize(const nlohmann::json& in) override;
#if defined(HIKARI_WITH_EDITOR)
    void BuildInspector(IInspectorBuilder& builder) override;
#endif

    void Play(std::string clip, bool loop = true, bool restart = true);
    void Play(ANIMATION::AnimationClipReference clip, bool loop = true, bool restart = true);
    void PlayMotion(ANIMATION::AnimationMotionSample motion, float speed = 1.0f, bool loop = true, bool restart = true);
    void CrossFade(std::string clip, float durationSeconds, bool loop = true, bool restart = true);
    void CrossFade(ANIMATION::AnimationClipReference clip, float durationSeconds, bool loop = true,
                   bool restart = true);
    void CrossFadeMotion(ANIMATION::AnimationMotionSample motion, float durationSeconds, float speed = 1.0f,
                         bool loop = true, bool restart = true);
    void UpdateMotion(ANIMATION::AnimationMotionSample motion, const ModelAsset& model);
    void PlayOnce(std::string clip, bool restart = true);
    void Pause();
    void Resume();
    void Stop();
    void Advance(float deltaTimeSec, float clipDurationSec = -1.0f);

    void SetClip(std::string clip);
    void SetClip(ANIMATION::AnimationClipReference clip);
    const ANIMATION::AnimationClipReference& GetClipReference() const;
    ANIMATION::AnimationMotionSample GetMotionSample() const noexcept;
    void BindClipToModel(const ModelAsset& model);

    void SetTime(float timeSec);
    float GetTime() const;

    void SetSpeed(float speed);
    float GetSpeed() const;

    void SetLoop(bool loop);
    bool GetLoop() const;

    void SetRootMotionEnabled(bool enabled) noexcept;
    bool IsRootMotionEnabled() const noexcept;

    // Runtime-only animation LOD gate. Disabled animators retain their
    // last published pose without advancing or resampling it. This state
    // is never serialized into authored Scene data.
    void SetRuntimeEvaluationEnabled(bool enabled) noexcept { runtimeEvaluationEnabled_ = enabled; }
    bool IsRuntimeEvaluationEnabled() const noexcept { return runtimeEvaluationEnabled_; }

    bool IsPlaying() const;

    bool IsFinished() const;
    bool IsFinished(float clipDurationSec) const;

    bool IsTransitioning() const noexcept;
    float GetTransitionWeight() const noexcept;
    ANIMATION::AnimationMotionSample GetTransitionSourceMotion() const noexcept;
    float GetTransitionSourceTime() const noexcept;
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
        ANIMATION::AnimationSubtreeRootAlignment rootAlignment = ANIMATION::AnimationSubtreeRootAlignment::ParentLocal;
        bool active = false;
    };

    // The game-facing bridge uses these narrow methods to drive one
    // transient upper-body (or other subtree) overlay.  The base
    // Animator timeline remains independent and may continue advancing.
    void SetMaskedPoseLayer(
        std::string clipName, std::string maskRootNodeName, float normalizedTime, float targetWeight,
        float blendSeconds,
        ANIMATION::AnimationSubtreeRootAlignment rootAlignment = ANIMATION::AnimationSubtreeRootAlignment::ParentLocal);

    // Resolved bridge path.  The model is supplied by the owning
    // ModelComponent so clip/root validation and subtree topology are
    // cached once instead of repeating string scans in every frame.
    bool SetMaskedPoseLayerResolved(
        std::string_view clipName, std::string_view maskRootNodeName, float normalizedTime, float targetWeight,
        float blendSeconds, const ModelAsset& model,
        ANIMATION::AnimationSubtreeRootAlignment rootAlignment = ANIMATION::AnimationSubtreeRootAlignment::ParentLocal);
    void ClearMaskedPoseLayer(float blendOutSeconds = 0.0f);
    const MaskedPoseLayerState& GetMaskedPoseLayer() const noexcept;

    // AnimationSystem uses the resolved cache to sample and blend the
    // overlay without allocating full-pose temporaries each frame.
    bool PrepareMaskedPoseLayer(const ModelAsset& model);
    ANIMATION::AnimationPoseSubtreeCache& GetMaskedPoseLayerCache() noexcept;
    const ANIMATION::AnimationPoseSubtreeCache& GetMaskedPoseLayerCache() const noexcept;

private:
    void ClearTransition() noexcept;
    void SetMaskedPoseLayerView(std::string_view clipName, std::string_view maskRootNodeName, float normalizedTime,
                                float targetWeight, float blendSeconds,
                                ANIMATION::AnimationSubtreeRootAlignment rootAlignment);
    void AdvanceMaskedPoseLayer(float deltaTimeSec) noexcept;
    void ResetMaskedPoseLayer() noexcept;

    ANIMATION::AnimationClipReference& Clip() noexcept { return settings_; }
    const ANIMATION::AnimationClipReference& Clip() const noexcept { return settings_; }

    AnimatorSettings settings_{};
    ANIMATION::AnimationClipReference secondaryClip_{};
    float secondaryWeight_ = 0.0f;
    bool runtimeEvaluationEnabled_ = true;

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
