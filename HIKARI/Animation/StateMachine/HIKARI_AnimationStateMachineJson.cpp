#include "Animation/StateMachine/HIKARI_AnimationStateMachineJson.h"

#include <string_view>

namespace HIKARI::ANIMATION {
    namespace {
        const char* ToString(AnimationParameterType value) noexcept {
            switch (value) {
            case AnimationParameterType::Float: return "Float";
            case AnimationParameterType::Integer: return "Integer";
            case AnimationParameterType::Trigger: return "Trigger";
            case AnimationParameterType::Bool:
            default: return "Bool";
            }
        }

        AnimationParameterType ParseParameterType(
            std::string_view value) noexcept {
            if (value == "Float") return AnimationParameterType::Float;
            if (value == "Integer") return AnimationParameterType::Integer;
            if (value == "Trigger") return AnimationParameterType::Trigger;
            return AnimationParameterType::Bool;
        }

        const char* ToString(AnimationParameterSource value) noexcept {
            switch (value) {
            case AnimationParameterSource::CharacterGrounded:
                return "CharacterGrounded";
            case AnimationParameterSource::CharacterMoving:
                return "CharacterMoving";
            case AnimationParameterSource::CharacterHorizontalSpeed:
                return "CharacterHorizontalSpeed";
            case AnimationParameterSource::CharacterVerticalSpeed:
                return "CharacterVerticalSpeed";
            case AnimationParameterSource::CharacterInputMagnitude:
                return "CharacterInputMagnitude";
            case AnimationParameterSource::CharacterSprinting:
                return "CharacterSprinting";
            case AnimationParameterSource::CharacterFalling:
                return "CharacterFalling";
            case AnimationParameterSource::Manual:
            default: return "Manual";
            }
        }

        AnimationParameterSource ParseParameterSource(
            std::string_view value) noexcept {
            if (value == "CharacterGrounded") {
                return AnimationParameterSource::CharacterGrounded;
            }
            if (value == "CharacterMoving") {
                return AnimationParameterSource::CharacterMoving;
            }
            if (value == "CharacterHorizontalSpeed") {
                return AnimationParameterSource::CharacterHorizontalSpeed;
            }
            if (value == "CharacterVerticalSpeed") {
                return AnimationParameterSource::CharacterVerticalSpeed;
            }
            if (value == "CharacterInputMagnitude") {
                return AnimationParameterSource::CharacterInputMagnitude;
            }
            if (value == "CharacterSprinting") {
                return AnimationParameterSource::CharacterSprinting;
            }
            if (value == "CharacterFalling") {
                return AnimationParameterSource::CharacterFalling;
            }
            return AnimationParameterSource::Manual;
        }

        const char* ToString(AnimationConditionOperator value) noexcept {
            switch (value) {
            case AnimationConditionOperator::IsFalse: return "IsFalse";
            case AnimationConditionOperator::Greater: return "Greater";
            case AnimationConditionOperator::GreaterOrEqual:
                return "GreaterOrEqual";
            case AnimationConditionOperator::Less: return "Less";
            case AnimationConditionOperator::LessOrEqual:
                return "LessOrEqual";
            case AnimationConditionOperator::Equal: return "Equal";
            case AnimationConditionOperator::NotEqual: return "NotEqual";
            case AnimationConditionOperator::Triggered: return "Triggered";
            case AnimationConditionOperator::IsTrue:
            default: return "IsTrue";
            }
        }

        AnimationConditionOperator ParseConditionOperator(
            std::string_view value) noexcept {
            if (value == "IsFalse") return AnimationConditionOperator::IsFalse;
            if (value == "Greater") return AnimationConditionOperator::Greater;
            if (value == "GreaterOrEqual") {
                return AnimationConditionOperator::GreaterOrEqual;
            }
            if (value == "Less") return AnimationConditionOperator::Less;
            if (value == "LessOrEqual") {
                return AnimationConditionOperator::LessOrEqual;
            }
            if (value == "Equal") return AnimationConditionOperator::Equal;
            if (value == "NotEqual") {
                return AnimationConditionOperator::NotEqual;
            }
            if (value == "Triggered") {
                return AnimationConditionOperator::Triggered;
            }
            return AnimationConditionOperator::IsTrue;
        }

        nlohmann::json SerializeValue(const AnimationParameterValue& value) {
            if (const bool* boolean = std::get_if<bool>(&value)) {
                return *boolean;
            }
            if (const float* number = std::get_if<float>(&value)) {
                return *number;
            }
            return std::get<int32_t>(value);
        }

        AnimationParameterValue DeserializeValue(
            const nlohmann::json& value,
            AnimationParameterType type) {
            switch (type) {
            case AnimationParameterType::Float:
                return value.is_number() ? value.get<float>() : 0.0f;
            case AnimationParameterType::Integer:
                return value.is_number_integer()
                    ? value.get<int32_t>()
                    : int32_t{ 0 };
            case AnimationParameterType::Bool:
            case AnimationParameterType::Trigger:
            default:
                return value.is_boolean() ? value.get<bool>() : false;
            }
        }

        nlohmann::json SerializeClip(const AnimationClipReference& clip) {
            return {
                { "modelAssetId", clip.modelAssetId.value },
                { "clipId", clip.clipId.value },
                { "fallbackName", clip.fallbackName }
            };
        }

        AnimationClipReference DeserializeClip(const nlohmann::json& json) {
            AnimationClipReference clip{};
            if (!json.is_object()) return clip;
            clip.modelAssetId.value = json.value("modelAssetId", "");
            clip.clipId.value = json.value("clipId", uint64_t{ 0 });
            clip.fallbackName = json.value("fallbackName", "");
            return clip;
        }

        nlohmann::json SerializeMotion(const AnimationStateMotion& motion) {
            if (const auto* clip = std::get_if<AnimationClipMotion>(&motion)) {
                return {
                    { "type", "Clip" },
                    { "clip", SerializeClip(clip->clip) }
                };
            }
            const auto& blendTree = std::get<
                AnimationBlendTree1DMotion>(motion);
            nlohmann::json samples = nlohmann::json::array();
            for (const AnimationBlendTree1DSample& sample :
                    blendTree.samples) {
                samples.push_back({
                    { "clip", SerializeClip(sample.clip) },
                    { "threshold", sample.threshold },
                    { "speedScale", sample.speedScale }
                });
            }
            return {
                { "type", "BlendTree1D" },
                { "parameterId", blendTree.parameterId.value },
                { "samples", std::move(samples) }
            };
        }

        AnimationStateMotion DeserializeMotion(const nlohmann::json& json) {
            if (!json.is_object() ||
                json.value("type", "Clip") == "Clip") {
                AnimationClipMotion motion{};
                if (json.is_object()) {
                    const auto clip = json.find("clip");
                    if (clip != json.end()) {
                        motion.clip = DeserializeClip(*clip);
                    }
                }
                return motion;
            }

            AnimationBlendTree1DMotion motion{};
            motion.parameterId.value = json.value("parameterId", 0u);
            const auto samples = json.find("samples");
            if (samples != json.end() && samples->is_array()) {
                for (const nlohmann::json& node : *samples) {
                    if (!node.is_object()) continue;
                    AnimationBlendTree1DSample sample{};
                    if (const auto clip = node.find("clip");
                        clip != node.end()) {
                        sample.clip = DeserializeClip(*clip);
                    }
                    sample.threshold = node.value("threshold", 0.0f);
                    sample.speedScale = node.value("speedScale", 1.0f);
                    motion.samples.push_back(std::move(sample));
                }
            }
            return motion;
        }
    }

    void SerializeAnimationStateMachineJson(
        const AnimationStateMachineDefinition& definition,
        nlohmann::json& out) {
        out = {
            { "name", definition.name },
            { "previewModelAssetId", definition.previewModelAssetId.value },
            { "entryStateId", definition.entryStateId.value },
            { "parameters", nlohmann::json::array() },
            { "states", nlohmann::json::array() },
            { "transitions", nlohmann::json::array() }
        };
        for (const AnimationParameterDefinition& parameter :
                definition.parameters) {
            out["parameters"].push_back({
                { "id", parameter.id.value },
                { "name", parameter.name },
                { "type", ToString(parameter.type) },
                { "source", ToString(parameter.source) },
                { "default", SerializeValue(parameter.defaultValue) }
            });
        }
        for (const AnimationState& state : definition.states) {
            out["states"].push_back({
                { "id", state.id.value },
                { "name", state.name },
                { "motion", SerializeMotion(state.motion) },
                { "speed", state.speed },
                { "loop", state.loop },
                { "editorX", state.editorX },
                { "editorY", state.editorY }
            });
        }
        for (const AnimationStateTransition& transition :
                definition.transitions) {
            nlohmann::json conditions = nlohmann::json::array();
            for (const AnimationTransitionCondition& condition :
                    transition.conditions) {
                conditions.push_back({
                    { "parameterId", condition.parameterId.value },
                    { "comparison", ToString(condition.comparison) },
                    { "threshold", SerializeValue(condition.threshold) }
                });
            }
            out["transitions"].push_back({
                { "id", transition.id.value },
                { "sourceStateId", transition.sourceStateId.value },
                { "targetStateId", transition.targetStateId.value },
                { "priority", transition.priority },
                { "blendDurationSec", transition.blendDurationSec },
                { "exitTimeNormalized", transition.exitTimeNormalized },
                { "requireExitTime", transition.requireExitTime },
                { "allowSelfTransition", transition.allowSelfTransition },
                { "conditions", std::move(conditions) }
            });
        }
    }

    bool DeserializeAnimationStateMachineJson(
        const nlohmann::json& input,
        AnimationStateMachineDefinition& out) {
        if (!input.is_object()) return false;
        AnimationStateMachineDefinition definition{};
        definition.name = input.value("name", definition.name);
        definition.previewModelAssetId.value = input.value(
            "previewModelAssetId", "");
        definition.entryStateId.value = input.value("entryStateId", 0u);

        if (const auto found = input.find("parameters");
            found != input.end() && found->is_array()) {
            for (const nlohmann::json& node : *found) {
                if (!node.is_object()) continue;
                AnimationParameterDefinition parameter{};
                parameter.id.value = node.value("id", 0u);
                parameter.name = node.value("name", "Parameter");
                parameter.type = ParseParameterType(
                    node.value("type", "Bool"));
                parameter.source = ParseParameterSource(
                    node.value("source", "Manual"));
                const auto value = node.find("default");
                parameter.defaultValue = value != node.end()
                    ? DeserializeValue(*value, parameter.type)
                    : MakeDefaultAnimationParameterValue(parameter.type);
                definition.parameters.push_back(std::move(parameter));
            }
        }
        if (const auto found = input.find("states");
            found != input.end() && found->is_array()) {
            for (const nlohmann::json& node : *found) {
                if (!node.is_object()) continue;
                AnimationState state{};
                state.id.value = node.value("id", 0u);
                state.name = node.value("name", "State");
                if (const auto motion = node.find("motion");
                    motion != node.end()) {
                    state.motion = DeserializeMotion(*motion);
                }
                state.speed = node.value("speed", 1.0f);
                state.loop = node.value("loop", true);
                state.editorX = node.value("editorX", 80.0f);
                state.editorY = node.value("editorY", 80.0f);
                definition.states.push_back(std::move(state));
            }
        }
        if (const auto found = input.find("transitions");
            found != input.end() && found->is_array()) {
            for (const nlohmann::json& node : *found) {
                if (!node.is_object()) continue;
                AnimationStateTransition transition{};
                transition.id.value = node.value("id", 0u);
                transition.sourceStateId.value = node.value(
                    "sourceStateId", 0u);
                transition.targetStateId.value = node.value(
                    "targetStateId", 0u);
                transition.priority = node.value("priority", 0);
                transition.blendDurationSec = node.value(
                    "blendDurationSec", 0.15f);
                transition.exitTimeNormalized = node.value(
                    "exitTimeNormalized", 0.9f);
                transition.requireExitTime = node.value(
                    "requireExitTime", false);
                transition.allowSelfTransition = node.value(
                    "allowSelfTransition", false);
                if (const auto conditions = node.find("conditions");
                    conditions != node.end() && conditions->is_array()) {
                    for (const nlohmann::json& conditionNode : *conditions) {
                        if (!conditionNode.is_object()) continue;
                        AnimationTransitionCondition condition{};
                        condition.parameterId.value = conditionNode.value(
                            "parameterId", 0u);
                        condition.comparison = ParseConditionOperator(
                            conditionNode.value("comparison", "IsTrue"));
                        const AnimationParameterDefinition* parameter =
                            FindAnimationParameter(
                                definition,
                                condition.parameterId);
                        const AnimationParameterType type = parameter != nullptr
                            ? parameter->type
                            : AnimationParameterType::Bool;
                        const auto threshold = conditionNode.find("threshold");
                        condition.threshold = threshold != conditionNode.end()
                            ? DeserializeValue(*threshold, type)
                            : MakeDefaultAnimationParameterValue(type);
                        transition.conditions.push_back(std::move(condition));
                    }
                }
                definition.transitions.push_back(std::move(transition));
            }
        }
        NormalizeAnimationStateMachine(definition);
        out = std::move(definition);
        return true;
    }

} // namespace HIKARI::ANIMATION
