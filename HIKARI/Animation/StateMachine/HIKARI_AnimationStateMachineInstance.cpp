#include "Animation/StateMachine/HIKARI_AnimationStateMachineInstance.h"

#include <cmath>
#include <utility>

namespace HIKARI::ANIMATION {
    namespace {
        constexpr float kFloatComparisonEpsilon = 0.0001f;

        bool CompareValues(
            const AnimationParameterValue& value,
            const AnimationParameterValue& threshold,
            AnimationConditionOperator comparison) noexcept {
            if (const bool* boolean = std::get_if<bool>(&value)) {
                const bool expected = std::get_if<bool>(&threshold) != nullptr
                    ? std::get<bool>(threshold)
                    : false;
                switch (comparison) {
                case AnimationConditionOperator::IsTrue:
                case AnimationConditionOperator::Triggered: return *boolean;
                case AnimationConditionOperator::IsFalse: return !*boolean;
                case AnimationConditionOperator::Equal:
                    return *boolean == expected;
                case AnimationConditionOperator::NotEqual:
                    return *boolean != expected;
                default: return false;
                }
            }

            const float lhs = std::holds_alternative<float>(value)
                ? std::get<float>(value)
                : static_cast<float>(std::get<int32_t>(value));
            const float rhs = std::holds_alternative<float>(threshold)
                ? std::get<float>(threshold)
                : std::holds_alternative<int32_t>(threshold)
                    ? static_cast<float>(std::get<int32_t>(threshold))
                    : 0.0f;
            switch (comparison) {
            case AnimationConditionOperator::Greater: return lhs > rhs;
            case AnimationConditionOperator::GreaterOrEqual:
                return lhs >= rhs;
            case AnimationConditionOperator::Less: return lhs < rhs;
            case AnimationConditionOperator::LessOrEqual: return lhs <= rhs;
            case AnimationConditionOperator::Equal:
                return std::fabs(lhs - rhs) <= kFloatComparisonEpsilon;
            case AnimationConditionOperator::NotEqual:
                return std::fabs(lhs - rhs) > kFloatComparisonEpsilon;
            default: return false;
            }
        }
    }

    bool AnimationStateMachineInstance::Bind(
        std::shared_ptr<const AnimationStateMachineAsset> asset) {
        if (asset_ == asset && asset_ != nullptr) return false;
        asset_ = std::move(asset);
        values_.clear();
        snapshot_ = {};
        pendingPlaybackRequest_ =
            AnimationStateMachinePlaybackRequest::None;
        if (asset_ == nullptr) {
            snapshot_.lastError = "Animation state machine asset is unavailable";
            return true;
        }
        snapshot_.assetGuid = asset_->guid;
        for (const AnimationParameterDefinition& parameter :
                asset_->definition.parameters) {
            values_[parameter.id.value] = parameter.defaultValue;
        }
        snapshot_.currentStateId = asset_->definition.entryStateId;
        if (const AnimationState* state = GetCurrentState()) {
            snapshot_.currentStateName = state->name;
            snapshot_.initialized = true;
        } else {
            snapshot_.lastError = "Animation state machine entry state is invalid";
        }
        return true;
    }

    void AnimationStateMachineInstance::Reset() {
        const std::shared_ptr<const AnimationStateMachineAsset> asset = asset_;
        asset_.reset();
        (void)Bind(asset);
    }

    const AnimationStateMachineAsset*
        AnimationStateMachineInstance::GetAsset() const noexcept {
        return asset_.get();
    }

    const AnimationState*
        AnimationStateMachineInstance::GetCurrentState() const noexcept {
        return asset_ != nullptr
            ? FindAnimationState(
                asset_->definition,
                snapshot_.currentStateId)
            : nullptr;
    }

    const AnimationStateMachineRuntimeSnapshot&
        AnimationStateMachineInstance::GetSnapshot() const noexcept {
        return snapshot_;
    }

    bool AnimationStateMachineInstance::Start(bool restart) {
        if (asset_ == nullptr || !snapshot_.initialized) return false;
        if (restart || !snapshot_.currentStateId.IsValid()) {
            const AnimationState* entry = FindAnimationState(
                asset_->definition,
                asset_->definition.entryStateId);
            if (entry == nullptr) {
                SetError("Animation state machine entry state is invalid");
                return false;
            }
            snapshot_.currentStateId = entry->id;
            snapshot_.currentStateName = entry->name;
        }
        snapshot_.running = true;
        snapshot_.lastError.clear();
        pendingPlaybackRequest_ =
            AnimationStateMachinePlaybackRequest::Start;
        return true;
    }

    bool AnimationStateMachineInstance::Stop() noexcept {
        if (!snapshot_.initialized) return false;
        snapshot_.running = false;
        pendingPlaybackRequest_ =
            AnimationStateMachinePlaybackRequest::Stop;
        return true;
    }

    AnimationStateMachinePlaybackRequest
        AnimationStateMachineInstance::ConsumePlaybackRequest() noexcept {
        const AnimationStateMachinePlaybackRequest request =
            pendingPlaybackRequest_;
        pendingPlaybackRequest_ =
            AnimationStateMachinePlaybackRequest::None;
        return request;
    }

    bool AnimationStateMachineInstance::SetBool(
        AnimationParameterId id,
        bool value) {
        return SetValue(id, AnimationParameterType::Bool, value);
    }

    bool AnimationStateMachineInstance::SetFloat(
        AnimationParameterId id,
        float value) {
        return SetValue(id, AnimationParameterType::Float, value);
    }

    bool AnimationStateMachineInstance::SetInteger(
        AnimationParameterId id,
        int32_t value) {
        return SetValue(id, AnimationParameterType::Integer, value);
    }

    bool AnimationStateMachineInstance::FireTrigger(AnimationParameterId id) {
        return SetValue(id, AnimationParameterType::Trigger, true);
    }

    bool AnimationStateMachineInstance::SetBool(
        std::string_view name,
        bool value) {
        return SetBool(FindParameterId(name), value);
    }

    bool AnimationStateMachineInstance::SetFloat(
        std::string_view name,
        float value) {
        return SetFloat(FindParameterId(name), value);
    }

    bool AnimationStateMachineInstance::SetInteger(
        std::string_view name,
        int32_t value) {
        return SetInteger(FindParameterId(name), value);
    }

    bool AnimationStateMachineInstance::FireTrigger(std::string_view name) {
        return FireTrigger(FindParameterId(name));
    }

    const AnimationParameterValue*
        AnimationStateMachineInstance::FindValue(
            AnimationParameterId id) const noexcept {
        const auto found = values_.find(id.value);
        return found != values_.end() ? &found->second : nullptr;
    }

    AnimationStateMotionEvaluation
        AnimationStateMachineInstance::ResolveCurrentMotion() {
        AnimationStateMotionEvaluation result{};
        const AnimationState* state = GetCurrentState();
        if (state == nullptr) return result;

        const AnimationParameterId parameterId =
            GetAnimationStateMotionParameter(state->motion);
        result = EvaluateAnimationStateMotion(
            state->motion,
            parameterId.IsValid() ? FindValue(parameterId) : nullptr);
        snapshot_.motionType = GetAnimationStateMotionType(state->motion);
        snapshot_.primaryClipName =
            result.sample.primaryClip.fallbackName;
        snapshot_.secondaryClipName =
            result.sample.secondaryClip.fallbackName;
        snapshot_.blendInputValue = result.inputValue;
        snapshot_.blendLowerThreshold = result.lowerThreshold;
        snapshot_.blendUpperThreshold = result.upperThreshold;
        snapshot_.motionBlendWeight =
            result.sample.secondaryWeight;
        snapshot_.effectiveSpeed = state->speed * result.speedScale;
        snapshot_.blendParameterName.clear();
        if (asset_ != nullptr && parameterId.IsValid()) {
            const AnimationParameterDefinition* parameter =
                FindAnimationParameter(asset_->definition, parameterId);
            if (parameter != nullptr) {
                snapshot_.blendParameterName = parameter->name;
            }
        }
        if (!result.valid) {
            snapshot_.lastError = "Current state motion cannot be evaluated";
        } else if (snapshot_.lastError ==
                "Current state motion cannot be evaluated") {
            snapshot_.lastError.clear();
        }
        return result;
    }

    const AnimationStateTransition* AnimationStateMachineInstance::Evaluate(
        float normalizedTime,
        bool clipFinished) const noexcept {
        if (asset_ == nullptr || !snapshot_.initialized ||
            !snapshot_.running) {
            return nullptr;
        }
        const float safeNormalizedTime = std::isfinite(normalizedTime)
            ? normalizedTime
            : 0.0f;
        const AnimationStateTransition* selected = nullptr;
        for (const AnimationStateTransition& transition :
                asset_->definition.transitions) {
            if (transition.sourceStateId.IsValid() &&
                transition.sourceStateId != snapshot_.currentStateId) {
                continue;
            }
            if (!transition.allowSelfTransition &&
                transition.targetStateId == snapshot_.currentStateId) {
                continue;
            }
            if (transition.requireExitTime && !clipFinished &&
                safeNormalizedTime < transition.exitTimeNormalized) {
                continue;
            }
            if (!ConditionsPass(transition)) continue;
            if (selected == nullptr ||
                transition.priority > selected->priority ||
                (transition.priority == selected->priority &&
                    transition.id.value < selected->id.value)) {
                selected = &transition;
            }
        }
        return selected;
    }

    bool AnimationStateMachineInstance::CommitTransition(
        AnimationStateId targetStateId) {
        if (asset_ == nullptr) return false;
        const AnimationState* target = FindAnimationState(
            asset_->definition,
            targetStateId);
        if (target == nullptr) {
            SetError("Animation transition target is missing");
            return false;
        }
        snapshot_.currentStateId = targetStateId;
        snapshot_.currentStateName = target->name;
        snapshot_.lastError.clear();
        ++snapshot_.transitionCount;
        return true;
    }

    void AnimationStateMachineInstance::ClearTriggers() noexcept {
        if (asset_ == nullptr) return;
        for (const AnimationParameterDefinition& parameter :
                asset_->definition.parameters) {
            if (parameter.type == AnimationParameterType::Trigger) {
                values_[parameter.id.value] = false;
            }
        }
    }

    void AnimationStateMachineInstance::SetError(std::string message) {
        snapshot_.lastError = std::move(message);
    }

    bool AnimationStateMachineInstance::SetValue(
        AnimationParameterId id,
        AnimationParameterType expectedType,
        AnimationParameterValue value) {
        if (asset_ == nullptr || !id.IsValid()) return false;
        const AnimationParameterDefinition* parameter =
            FindAnimationParameter(asset_->definition, id);
        if (parameter == nullptr || parameter->type != expectedType) {
            return false;
        }
        values_[id.value] = std::move(value);
        return true;
    }

    AnimationParameterId AnimationStateMachineInstance::FindParameterId(
        std::string_view name) const noexcept {
        const AnimationParameterDefinition* parameter = asset_ != nullptr
            ? FindAnimationParameter(asset_->definition, name)
            : nullptr;
        return parameter != nullptr ? parameter->id : AnimationParameterId{};
    }

    bool AnimationStateMachineInstance::ConditionsPass(
        const AnimationStateTransition& transition) const noexcept {
        for (const AnimationTransitionCondition& condition :
                transition.conditions) {
            const AnimationParameterValue* value = FindValue(
                condition.parameterId);
            if (value == nullptr || !CompareValues(
                    *value,
                    condition.threshold,
                    condition.comparison)) {
                return false;
            }
        }
        return true;
    }

} // namespace HIKARI::ANIMATION
