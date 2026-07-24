#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Assets/Animation/HIKARI_AnimationStateMachineAsset.h"
#include "Animation/StateMachine/HIKARI_AnimationStateMotionEvaluator.h"

namespace HIKARI::ANIMATION {

    enum class AnimationStateMachinePlaybackRequest : uint8_t {
        None,
        Start,
        Stop,
    };

    struct AnimationStateMachineRuntimeSnapshot {
        AssetGuid assetGuid{};
        AnimationStateId currentStateId{};
        std::string currentStateName{};
        std::string primaryClipName{};
        std::string secondaryClipName{};
        std::string blendParameterName{};
        std::string lastError{};
        AnimationStateMotionType motionType = AnimationStateMotionType::Clip;
        float blendInputValue = 0.0f;
        float blendLowerThreshold = 0.0f;
        float blendUpperThreshold = 0.0f;
        float motionBlendWeight = 0.0f;
        float effectiveSpeed = 1.0f;
        uint64_t transitionCount = 0u;
        bool initialized = false;
        bool running = false;
    };

    class AnimationStateMachineInstance {
    public:
        bool Bind(std::shared_ptr<const AnimationStateMachineAsset> asset);
        void Reset();

        const AnimationStateMachineAsset* GetAsset() const noexcept;
        const AnimationState* GetCurrentState() const noexcept;
        const AnimationStateMachineRuntimeSnapshot& GetSnapshot()
            const noexcept;

        bool Start(bool restart = true);
        bool Stop() noexcept;
        AnimationStateMachinePlaybackRequest ConsumePlaybackRequest()
            noexcept;

        bool SetBool(AnimationParameterId id, bool value);
        bool SetFloat(AnimationParameterId id, float value);
        bool SetInteger(AnimationParameterId id, int32_t value);
        bool FireTrigger(AnimationParameterId id);
        bool SetBool(std::string_view name, bool value);
        bool SetFloat(std::string_view name, float value);
        bool SetInteger(std::string_view name, int32_t value);
        bool FireTrigger(std::string_view name);

        const AnimationParameterValue* FindValue(
            AnimationParameterId id) const noexcept;
        AnimationStateMotionEvaluation ResolveCurrentMotion();
        const AnimationStateTransition* Evaluate(
            float normalizedTime,
            bool clipFinished) const noexcept;
        bool CommitTransition(AnimationStateId targetStateId);
        void ClearTriggers() noexcept;
        void SetError(std::string message);

    private:
        bool SetValue(
            AnimationParameterId id,
            AnimationParameterType expectedType,
            AnimationParameterValue value);
        AnimationParameterId FindParameterId(
            std::string_view name) const noexcept;
        bool ConditionsPass(
            const AnimationStateTransition& transition) const noexcept;

        std::shared_ptr<const AnimationStateMachineAsset> asset_{};
        std::unordered_map<uint32_t, AnimationParameterValue> values_{};
        AnimationStateMachineRuntimeSnapshot snapshot_{};
        AnimationStateMachinePlaybackRequest pendingPlaybackRequest_ =
            AnimationStateMachinePlaybackRequest::None;
    };

} // namespace HIKARI::ANIMATION
