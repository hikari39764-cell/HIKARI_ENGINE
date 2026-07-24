#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "Animation/Runtime/HIKARI_AnimationPose.h"
#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI::ANIMATION {

    struct AnimationStateId {
        uint32_t value = 0u;
        bool IsValid() const noexcept { return value != 0u; }
        friend bool operator==(const AnimationStateId&, const AnimationStateId&) = default;
    };

    struct AnimationParameterId {
        uint32_t value = 0u;
        bool IsValid() const noexcept { return value != 0u; }
        friend bool operator==(const AnimationParameterId&, const AnimationParameterId&) = default;
    };

    struct AnimationTransitionId {
        uint32_t value = 0u;
        bool IsValid() const noexcept { return value != 0u; }
        friend bool operator==(const AnimationTransitionId&, const AnimationTransitionId&) = default;
    };

    enum class AnimationParameterType : uint8_t {
        Bool,
        Float,
        Integer,
        Trigger,
    };

    // Optional built-in input. Manual parameters remain fully controlled by
    // game C++ through AnimationStateMachineRuntimeService.
    enum class AnimationParameterSource : uint8_t {
        Manual,
        CharacterGrounded,
        CharacterMoving,
        CharacterHorizontalSpeed,
        CharacterVerticalSpeed,
        CharacterInputMagnitude,
        CharacterSprinting,
        CharacterFalling,
    };

    using AnimationParameterValue = std::variant<bool, float, int32_t>;

    struct AnimationParameterDefinition {
        AnimationParameterId id{};
        std::string name{ "Parameter" };
        AnimationParameterType type = AnimationParameterType::Bool;
        AnimationParameterSource source = AnimationParameterSource::Manual;
        AnimationParameterValue defaultValue{ false };
    };

    enum class AnimationStateMotionType : uint8_t {
        Clip,
        BlendTree1D,
    };

    struct AnimationClipMotion {
        AnimationClipReference clip{};
    };

    struct AnimationBlendTree1DSample {
        AnimationClipReference clip{};
        float threshold = 0.0f;
        float speedScale = 1.0f;
    };

    struct AnimationBlendTree1DMotion {
        AnimationParameterId parameterId{};
        std::vector<AnimationBlendTree1DSample> samples{};
    };

    using AnimationStateMotion = std::variant<
        AnimationClipMotion,
        AnimationBlendTree1DMotion>;

    struct AnimationState {
        AnimationStateId id{};
        std::string name{ "State" };
        AnimationStateMotion motion{ AnimationClipMotion{} };
        float speed = 1.0f;
        bool loop = true;
        float editorX = 80.0f;
        float editorY = 80.0f;
    };

    enum class AnimationConditionOperator : uint8_t {
        IsTrue,
        IsFalse,
        Greater,
        GreaterOrEqual,
        Less,
        LessOrEqual,
        Equal,
        NotEqual,
        Triggered,
    };

    struct AnimationTransitionCondition {
        AnimationParameterId parameterId{};
        AnimationConditionOperator comparison =
            AnimationConditionOperator::IsTrue;
        AnimationParameterValue threshold{ false };
    };

    struct AnimationStateTransition {
        AnimationTransitionId id{};
        // Invalid source means Any State.
        AnimationStateId sourceStateId{};
        AnimationStateId targetStateId{};
        int priority = 0;
        float blendDurationSec = 0.15f;
        float exitTimeNormalized = 0.9f;
        bool requireExitTime = false;
        bool allowSelfTransition = false;
        std::vector<AnimationTransitionCondition> conditions{};
    };

    struct AnimationStateMachineDefinition {
        std::string name{ "New Animation State Machine" };
        AssetId previewModelAssetId{};
        AnimationStateId entryStateId{};
        std::vector<AnimationParameterDefinition> parameters{};
        std::vector<AnimationState> states{};
        std::vector<AnimationStateTransition> transitions{};
    };

    struct AnimationStateMachineValidationIssue {
        std::string message{};
        bool error = true;
    };

    AnimationStateId AllocateAnimationStateId(
        const AnimationStateMachineDefinition& definition) noexcept;
    AnimationParameterId AllocateAnimationParameterId(
        const AnimationStateMachineDefinition& definition) noexcept;
    AnimationTransitionId AllocateAnimationTransitionId(
        const AnimationStateMachineDefinition& definition) noexcept;

    AnimationState* FindAnimationState(
        AnimationStateMachineDefinition& definition,
        AnimationStateId id) noexcept;
    const AnimationState* FindAnimationState(
        const AnimationStateMachineDefinition& definition,
        AnimationStateId id) noexcept;
    AnimationParameterDefinition* FindAnimationParameter(
        AnimationStateMachineDefinition& definition,
        AnimationParameterId id) noexcept;
    const AnimationParameterDefinition* FindAnimationParameter(
        const AnimationStateMachineDefinition& definition,
        AnimationParameterId id) noexcept;
    const AnimationParameterDefinition* FindAnimationParameter(
        const AnimationStateMachineDefinition& definition,
        std::string_view name) noexcept;

    void NormalizeAnimationStateMachine(
        AnimationStateMachineDefinition& definition);
    std::vector<AnimationStateMachineValidationIssue>
        ValidateAnimationStateMachine(
            const AnimationStateMachineDefinition& definition);

    bool IsAnimationConditionCompatible(
        AnimationParameterType parameterType,
        AnimationConditionOperator comparison) noexcept;
    AnimationParameterValue MakeDefaultAnimationParameterValue(
        AnimationParameterType type) noexcept;

} // namespace HIKARI::ANIMATION
