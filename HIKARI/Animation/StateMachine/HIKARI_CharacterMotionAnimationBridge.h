#pragma once

namespace HIKARI::GAMEPLAY { struct CharacterMotionState; }

namespace HIKARI::ANIMATION {

    class AnimationStateMachineInstance;

    void ApplyCharacterMotionAnimationParameters(
        AnimationStateMachineInstance& instance,
        const GAMEPLAY::CharacterMotionState& motionState);

} // namespace HIKARI::ANIMATION
