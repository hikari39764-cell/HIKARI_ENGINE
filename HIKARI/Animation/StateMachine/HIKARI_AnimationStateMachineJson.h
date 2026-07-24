#pragma once

#include <json.hpp>

#include "Animation/StateMachine/HIKARI_AnimationStateMachine.h"

namespace HIKARI::ANIMATION {

    void SerializeAnimationStateMachineJson(
        const AnimationStateMachineDefinition& definition,
        nlohmann::json& out);
    bool DeserializeAnimationStateMachineJson(
        const nlohmann::json& input,
        AnimationStateMachineDefinition& out);

} // namespace HIKARI::ANIMATION
