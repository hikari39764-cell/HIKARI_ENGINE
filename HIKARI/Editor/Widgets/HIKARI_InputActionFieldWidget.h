#pragma once

#include <string>
#include <string_view>

#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI::INPUT {
    class InputActionMap;
}

namespace HIKARI::EDITOR {

    bool DrawInputActionField(
        const INPUT::InputActionMap& actionMap,
        std::string_view label,
        INPUT::InputActionValueType expectedType,
        std::string& inOutActionId);

} // namespace HIKARI::EDITOR
