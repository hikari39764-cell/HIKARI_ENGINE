#include "Animation/StateMachine/HIKARI_AnimationStateMotionEvaluator.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::ANIMATION {
    namespace {
        float ReadBlendValue(
            const AnimationParameterValue* value) noexcept {
            if (value == nullptr) return 0.0f;
            if (const float* number = std::get_if<float>(value)) {
                return std::isfinite(*number) ? *number : 0.0f;
            }
            if (const int32_t* integer = std::get_if<int32_t>(value)) {
                return static_cast<float>(*integer);
            }
            return std::get<bool>(*value) ? 1.0f : 0.0f;
        }

        float SafeSpeedScale(float value) noexcept {
            return std::isfinite(value) ? value : 1.0f;
        }
    }

    AnimationStateMotionType GetAnimationStateMotionType(
        const AnimationStateMotion& motion) noexcept {
        return std::holds_alternative<AnimationBlendTree1DMotion>(motion)
            ? AnimationStateMotionType::BlendTree1D
            : AnimationStateMotionType::Clip;
    }

    AnimationParameterId GetAnimationStateMotionParameter(
        const AnimationStateMotion& motion) noexcept {
        const auto* blendTree = std::get_if<
            AnimationBlendTree1DMotion>(&motion);
        return blendTree != nullptr
            ? blendTree->parameterId
            : AnimationParameterId{};
    }

    bool IsAnimationStateMotionEmpty(
        const AnimationStateMotion& motion) noexcept {
        if (const auto* clip = std::get_if<AnimationClipMotion>(&motion)) {
            return clip->clip.IsEmpty();
        }
        const auto* blendTree = std::get_if<
            AnimationBlendTree1DMotion>(&motion);
        if (blendTree == nullptr || blendTree->samples.empty()) return true;
        return std::all_of(
            blendTree->samples.begin(),
            blendTree->samples.end(),
            [](const AnimationBlendTree1DSample& sample) {
                return sample.clip.IsEmpty();
            });
    }

    AnimationStateMotionEvaluation EvaluateAnimationStateMotion(
        const AnimationStateMotion& motion,
        const AnimationParameterValue* parameterValue) noexcept {
        AnimationStateMotionEvaluation result{};
        if (const auto* clip = std::get_if<AnimationClipMotion>(&motion)) {
            result.sample.primaryClip = clip->clip;
            result.valid = !clip->clip.IsEmpty();
            return result;
        }

        const auto* blendTree = std::get_if<
            AnimationBlendTree1DMotion>(&motion);
        if (blendTree == nullptr || blendTree->samples.empty()) return result;

        result.inputValue = ReadBlendValue(parameterValue);
        const auto& samples = blendTree->samples;
        const auto assignSingle = [&result](
            const AnimationBlendTree1DSample& sample) {
            result.sample.primaryClip = sample.clip;
            result.speedScale = SafeSpeedScale(sample.speedScale);
            result.lowerThreshold = sample.threshold;
            result.upperThreshold = sample.threshold;
            result.valid = !sample.clip.IsEmpty();
        };

        if (samples.size() == 1u ||
            result.inputValue <= samples.front().threshold) {
            assignSingle(samples.front());
            return result;
        }
        if (result.inputValue >= samples.back().threshold) {
            assignSingle(samples.back());
            return result;
        }

        const auto upper = std::upper_bound(
            samples.begin(),
            samples.end(),
            result.inputValue,
            [](float value, const AnimationBlendTree1DSample& sample) {
                return value < sample.threshold;
            });
        if (upper == samples.begin() || upper == samples.end()) {
            assignSingle(upper == samples.end() ? samples.back() : *upper);
            return result;
        }
        const auto& right = *upper;
        const auto& left = *(upper - 1);
        const float span = right.threshold - left.threshold;
        const float weight = span > 1.0e-5f
            ? std::clamp(
                (result.inputValue - left.threshold) / span,
                0.0f,
                1.0f)
            : 1.0f;
        result.sample.primaryClip = left.clip;
        result.sample.secondaryClip = right.clip;
        result.sample.secondaryWeight = weight;
        result.speedScale = std::lerp(
            SafeSpeedScale(left.speedScale),
            SafeSpeedScale(right.speedScale),
            weight);
        result.lowerThreshold = left.threshold;
        result.upperThreshold = right.threshold;
        result.valid = !left.clip.IsEmpty() && !right.clip.IsEmpty();
        return result;
    }

} // namespace HIKARI::ANIMATION
