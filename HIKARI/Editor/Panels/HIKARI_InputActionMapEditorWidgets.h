#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "Input/Assets/HIKARI_InputActionMap.h"

namespace HIKARI::EDITOR::INPUT_WIDGETS {

bool EditString(const char* label, std::string& value, size_t capacity = 192);
bool MatchesFilter(std::string_view value, const char* filter);
bool DrawValueTypeCombo(
    const char* label,
    INPUT::InputActionValueType& valueType);
bool DrawBindingSourceCombo(
    const char* label,
    INPUT::InputBindingSource& source);
std::string DefaultControl(INPUT::InputBindingSource source);
bool DrawControlPicker(
    const char* label,
    INPUT::InputBindingSource source,
    std::string& control);
bool DrawModifierPicker(std::string& modifier);
const char* DeviceLabel(INPUT::InputDeviceKind device);

} // namespace HIKARI::EDITOR::INPUT_WIDGETS
