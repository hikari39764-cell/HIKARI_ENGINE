#pragma once

#include "Animation/StateMachine/HIKARI_AnimationStateMachine.h"

namespace HIKARI::ANIMATION {

    struct AnimationStateMotionEvaluation {
        AnimationMotionSample sample{};
        float speedScale = 1.0f;
        float inputValue = 0.0f;
        float lowerThreshold = 0.0f;
        float upperThreshold = 0.0f;
        bool valid = false;
    };

    AnimationStateMotionType GetAnimationStateMotionType(
        const AnimationStateMotion& motion) noexcept;
    AnimationParameterId GetAnimationStateMotionParameter(
        const AnimationStateMotion& motion) noexcept;
    bool IsAnimationStateMotionEmpty(
        const AnimationStateMotion& motion) noexcept;

    AnimationStateMotionEvaluation EvaluateAnimationStateMotion(
        const AnimationStateMotion& motion,
        const AnimationParameterValue* parameterValue) noexcept;

} // namespace HIKARI::ANIMATION
