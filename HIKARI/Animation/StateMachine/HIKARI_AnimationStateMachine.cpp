#include "Animation/StateMachine/HIKARI_AnimationStateMachine.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "Animation/StateMachine/HIKARI_AnimationStateMotionEvaluator.h"

namespace HIKARI::ANIMATION {
    namespace {
        template<class TId, class TCollection, class TGetId>
        TId AllocateId(const TCollection& collection, TGetId&& getId) noexcept {
            uint32_t next = 1u;
            for (const auto& value : collection) {
                const uint32_t current = getId(value).value;
                if (current >= next) next = current + 1u;
            }
            return { next != 0u ? next : 1u };
        }

        std::string MakeUniqueName(
            std::string requested,
            std::string_view fallback,
            std::unordered_set<std::string>& used) {
            if (requested.empty()) requested = std::string(fallback);
            if (used.insert(requested).second) return requested;
            const std::string base = requested;
            for (uint32_t suffix = 2u;; ++suffix) {
                requested = base + " " + std::to_string(suffix);
                if (used.insert(requested).second) return requested;
            }
        }

        bool HasState(
            const AnimationStateMachineDefinition& definition,
            AnimationStateId id) noexcept {
            return FindAnimationState(definition, id) != nullptr;
        }

        void NormalizeStateMotion(AnimationStateMotion& motion) {
            auto* blendTree = std::get_if<
                AnimationBlendTree1DMotion>(&motion);
            if (blendTree == nullptr) return;
            for (AnimationBlendTree1DSample& sample : blendTree->samples) {
                if (!std::isfinite(sample.threshold)) sample.threshold = 0.0f;
                if (!std::isfinite(sample.speedScale)) sample.speedScale = 1.0f;
            }
            std::stable_sort(
                blendTree->samples.begin(),
                blendTree->samples.end(),
                [](const AnimationBlendTree1DSample& lhs,
                    const AnimationBlendTree1DSample& rhs) {
                    return lhs.threshold < rhs.threshold;
                });
        }
    }

    AnimationStateId AllocateAnimationStateId(
        const AnimationStateMachineDefinition& definition) noexcept {
        return AllocateId<AnimationStateId>(
            definition.states,
            [](const AnimationState& state) { return state.id; });
    }

    AnimationParameterId AllocateAnimationParameterId(
        const AnimationStateMachineDefinition& definition) noexcept {
        return AllocateId<AnimationParameterId>(
            definition.parameters,
            [](const AnimationParameterDefinition& parameter) {
                return parameter.id;
            });
    }

    AnimationTransitionId AllocateAnimationTransitionId(
        const AnimationStateMachineDefinition& definition) noexcept {
        return AllocateId<AnimationTransitionId>(
            definition.transitions,
            [](const AnimationStateTransition& transition) {
                return transition.id;
            });
    }

    AnimationState* FindAnimationState(
        AnimationStateMachineDefinition& definition,
        AnimationStateId id) noexcept {
        const auto found = std::find_if(
            definition.states.begin(),
            definition.states.end(),
            [id](const AnimationState& state) { return state.id == id; });
        return found != definition.states.end() ? &*found : nullptr;
    }

    const AnimationState* FindAnimationState(
        const AnimationStateMachineDefinition& definition,
        AnimationStateId id) noexcept {
        const auto found = std::find_if(
            definition.states.begin(),
            definition.states.end(),
            [id](const AnimationState& state) { return state.id == id; });
        return found != definition.states.end() ? &*found : nullptr;
    }

    AnimationParameterDefinition* FindAnimationParameter(
        AnimationStateMachineDefinition& definition,
        AnimationParameterId id) noexcept {
        const auto found = std::find_if(
            definition.parameters.begin(),
            definition.parameters.end(),
            [id](const AnimationParameterDefinition& parameter) {
                return parameter.id == id;
            });
        return found != definition.parameters.end() ? &*found : nullptr;
    }

    const AnimationParameterDefinition* FindAnimationParameter(
        const AnimationStateMachineDefinition& definition,
        AnimationParameterId id) noexcept {
        const auto found = std::find_if(
            definition.parameters.begin(),
            definition.parameters.end(),
            [id](const AnimationParameterDefinition& parameter) {
                return parameter.id == id;
            });
        return found != definition.parameters.end() ? &*found : nullptr;
    }

    const AnimationParameterDefinition* FindAnimationParameter(
        const AnimationStateMachineDefinition& definition,
        std::string_view name) noexcept {
        const auto found = std::find_if(
            definition.parameters.begin(),
            definition.parameters.end(),
            [name](const AnimationParameterDefinition& parameter) {
                return parameter.name == name;
            });
        return found != definition.parameters.end() ? &*found : nullptr;
    }

    bool IsAnimationConditionCompatible(
        AnimationParameterType parameterType,
        AnimationConditionOperator comparison) noexcept {
        if (parameterType == AnimationParameterType::Trigger) {
            return comparison == AnimationConditionOperator::Triggered;
        }
        if (parameterType == AnimationParameterType::Bool) {
            return comparison == AnimationConditionOperator::IsTrue ||
                comparison == AnimationConditionOperator::IsFalse ||
                comparison == AnimationConditionOperator::Equal ||
                comparison == AnimationConditionOperator::NotEqual;
        }
        return comparison == AnimationConditionOperator::Greater ||
            comparison == AnimationConditionOperator::GreaterOrEqual ||
            comparison == AnimationConditionOperator::Less ||
            comparison == AnimationConditionOperator::LessOrEqual ||
            comparison == AnimationConditionOperator::Equal ||
            comparison == AnimationConditionOperator::NotEqual;
    }

    AnimationParameterValue MakeDefaultAnimationParameterValue(
        AnimationParameterType type) noexcept {
        switch (type) {
        case AnimationParameterType::Float: return 0.0f;
        case AnimationParameterType::Integer: return int32_t{ 0 };
        case AnimationParameterType::Bool:
        case AnimationParameterType::Trigger:
        default: return false;
        }
    }

    void NormalizeAnimationStateMachine(
        AnimationStateMachineDefinition& definition) {
        if (definition.name.empty()) {
            definition.name = "Animation State Machine";
        }

        std::unordered_set<uint32_t> stateIds{};
        std::unordered_set<std::string> stateNames{};
        uint32_t nextStateId = 1u;
        for (AnimationState& state : definition.states) {
            if (!state.id.IsValid() || !stateIds.insert(state.id.value).second) {
                while (stateIds.contains(nextStateId)) ++nextStateId;
                state.id.value = nextStateId++;
                stateIds.insert(state.id.value);
            }
            state.name = MakeUniqueName(
                std::move(state.name), "State", stateNames);
            NormalizeStateMotion(state.motion);
            if (!std::isfinite(state.speed)) state.speed = 1.0f;
            if (!std::isfinite(state.editorX)) state.editorX = 80.0f;
            if (!std::isfinite(state.editorY)) state.editorY = 80.0f;
        }
        if (definition.states.empty()) {
            AnimationState state{};
            state.id = { 1u };
            state.name = "Idle";
            definition.states.push_back(std::move(state));
        }
        if (!HasState(definition, definition.entryStateId)) {
            definition.entryStateId = definition.states.front().id;
        }

        std::unordered_set<uint32_t> parameterIds{};
        std::unordered_set<std::string> parameterNames{};
        uint32_t nextParameterId = 1u;
        for (AnimationParameterDefinition& parameter : definition.parameters) {
            if (!parameter.id.IsValid() ||
                !parameterIds.insert(parameter.id.value).second) {
                while (parameterIds.contains(nextParameterId)) {
                    ++nextParameterId;
                }
                parameter.id.value = nextParameterId++;
                parameterIds.insert(parameter.id.value);
            }
            parameter.name = MakeUniqueName(
                std::move(parameter.name), "Parameter", parameterNames);
            if (parameter.type == AnimationParameterType::Trigger) {
                parameter.source = AnimationParameterSource::Manual;
            }
            const AnimationParameterValue fallback =
                MakeDefaultAnimationParameterValue(parameter.type);
            if (parameter.type == AnimationParameterType::Float &&
                !std::holds_alternative<float>(parameter.defaultValue)) {
                parameter.defaultValue = fallback;
            } else if (parameter.type == AnimationParameterType::Integer &&
                !std::holds_alternative<int32_t>(parameter.defaultValue)) {
                parameter.defaultValue = fallback;
            } else if ((parameter.type == AnimationParameterType::Bool ||
                    parameter.type == AnimationParameterType::Trigger) &&
                !std::holds_alternative<bool>(parameter.defaultValue)) {
                parameter.defaultValue = fallback;
            }
        }

        std::unordered_set<uint32_t> transitionIds{};
        uint32_t nextTransitionId = 1u;
        for (AnimationStateTransition& transition : definition.transitions) {
            if (!transition.id.IsValid() ||
                !transitionIds.insert(transition.id.value).second) {
                while (transitionIds.contains(nextTransitionId)) {
                    ++nextTransitionId;
                }
                transition.id.value = nextTransitionId++;
                transitionIds.insert(transition.id.value);
            }
            transition.blendDurationSec = std::isfinite(
                transition.blendDurationSec)
                ? (std::max)(0.0f, transition.blendDurationSec)
                : 0.0f;
            transition.exitTimeNormalized = std::isfinite(
                transition.exitTimeNormalized)
                ? std::clamp(transition.exitTimeNormalized, 0.0f, 1.0f)
                : 0.9f;
            transition.conditions.erase(
                std::remove_if(
                    transition.conditions.begin(),
                    transition.conditions.end(),
                    [&definition](const AnimationTransitionCondition& condition) {
                        const AnimationParameterDefinition* parameter =
                            FindAnimationParameter(
                                definition,
                                condition.parameterId);
                        return parameter == nullptr ||
                            !IsAnimationConditionCompatible(
                                parameter->type,
                                condition.comparison);
                    }),
                transition.conditions.end());
        }
    }

    std::vector<AnimationStateMachineValidationIssue>
        ValidateAnimationStateMachine(
            const AnimationStateMachineDefinition& definition) {
        std::vector<AnimationStateMachineValidationIssue> issues{};
        if (definition.states.empty()) {
            issues.push_back({ "State machine has no states", true });
        }
        if (!HasState(definition, definition.entryStateId)) {
            issues.push_back({ "Entry state does not exist", true });
        }
        for (const AnimationState& state : definition.states) {
            if (IsAnimationStateMotionEmpty(state.motion)) {
                issues.push_back({
                    "State '" + state.name + "' has no animation motion",
                    false
                });
            }
            const auto* blendTree = std::get_if<
                AnimationBlendTree1DMotion>(&state.motion);
            if (blendTree == nullptr) continue;
            const AnimationParameterDefinition* parameter =
                FindAnimationParameter(definition, blendTree->parameterId);
            if (parameter == nullptr) {
                issues.push_back({
                    "State '" + state.name +
                        "' blend tree parameter is missing",
                    true
                });
            } else if (parameter->type != AnimationParameterType::Float) {
                issues.push_back({
                    "State '" + state.name +
                        "' blend tree requires a Float parameter",
                    true
                });
            }
            for (size_t index = 0u;
                    index < blendTree->samples.size(); ++index) {
                if (blendTree->samples[index].clip.IsEmpty()) {
                    issues.push_back({
                        "State '" + state.name +
                            "' blend sample has no animation clip",
                        false
                    });
                }
                if (index > 0u && std::abs(
                        blendTree->samples[index].threshold -
                        blendTree->samples[index - 1u].threshold) <=
                        1.0e-5f) {
                    issues.push_back({
                        "State '" + state.name +
                            "' has duplicate blend thresholds",
                        true
                    });
                }
            }
        }
        for (const AnimationStateTransition& transition :
                definition.transitions) {
            if (transition.sourceStateId.IsValid() &&
                !HasState(definition, transition.sourceStateId)) {
                issues.push_back({ "Transition source state is missing", true });
            }
            if (!HasState(definition, transition.targetStateId)) {
                issues.push_back({ "Transition target state is missing", true });
            }
            for (const AnimationTransitionCondition& condition :
                    transition.conditions) {
                const AnimationParameterDefinition* parameter =
                    FindAnimationParameter(definition, condition.parameterId);
                if (parameter == nullptr) {
                    issues.push_back({ "Transition parameter is missing", true });
                } else if (!IsAnimationConditionCompatible(
                        parameter->type,
                        condition.comparison)) {
                    issues.push_back({
                        "Transition condition is incompatible with parameter '" +
                            parameter->name + "'",
                        true
                    });
                }
            }
        }
        return issues;
    }

} // namespace HIKARI::ANIMATION
