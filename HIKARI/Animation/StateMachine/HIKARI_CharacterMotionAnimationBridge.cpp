#include "Animation/StateMachine/HIKARI_CharacterMotionAnimationBridge.h"

#include "Animation/StateMachine/HIKARI_AnimationStateMachineInstance.h"
#include "Gameplay/Motion/HIKARI_CharacterMotionState.h"

namespace HIKARI::ANIMATION {

    void ApplyCharacterMotionAnimationParameters(
        AnimationStateMachineInstance& instance,
        const GAMEPLAY::CharacterMotionState& motionState) {
        const AnimationStateMachineAsset* asset = instance.GetAsset();
        if (asset == nullptr) return;
        for (const AnimationParameterDefinition& parameter :
                asset->definition.parameters) {
            switch (parameter.source) {
            case AnimationParameterSource::CharacterGrounded:
                (void)instance.SetBool(
                    parameter.id,
                    motionState.IsGrounded());
                break;
            case AnimationParameterSource::CharacterMoving:
                (void)instance.SetBool(parameter.id, motionState.moving);
                break;
            case AnimationParameterSource::CharacterHorizontalSpeed:
                (void)instance.SetFloat(
                    parameter.id,
                    motionState.horizontalSpeed);
                break;
            case AnimationParameterSource::CharacterVerticalSpeed:
                (void)instance.SetFloat(
                    parameter.id,
                    motionState.verticalSpeed);
                break;
            case AnimationParameterSource::CharacterInputMagnitude:
                (void)instance.SetFloat(
                    parameter.id,
                    motionState.inputMagnitude);
                break;
            case AnimationParameterSource::CharacterSprinting:
                (void)instance.SetBool(parameter.id, motionState.sprinting);
                break;
            case AnimationParameterSource::CharacterFalling:
                (void)instance.SetBool(
                    parameter.id,
                    !motionState.IsGrounded() &&
                        motionState.verticalSpeed < -0.01f);
                break;
            case AnimationParameterSource::Manual:
            default:
                break;
            }
        }
    }

} // namespace HIKARI::ANIMATION
