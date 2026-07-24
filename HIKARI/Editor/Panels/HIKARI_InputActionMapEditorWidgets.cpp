#include "Editor/Panels/HIKARI_InputActionMapEditorWidgets.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <vector>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR::INPUT_WIDGETS {
namespace {


const std::vector<std::string>& KeyboardControls() {
    static const std::vector<std::string> controls = [] {
        std::vector<std::string> values{
            "W", "A", "S", "D", "Q", "E",
            "Up", "Down", "Left", "Right",
            "Space", "Enter", "Escape", "Tab", "Backspace",
            "Shift", "Ctrl", "Alt", "Home", "End",
            "PageUp", "PageDown", "Insert", "Delete",
        };
        for (char c = 'A'; c <= 'Z'; ++c) {
            const std::string value(1, c);
            if (std::find(values.begin(), values.end(), value) == values.end()) {
                values.push_back(value);
            }
        }
        for (char c = '0'; c <= '9'; ++c) values.emplace_back(1, c);
        for (int index = 1; index <= 12; ++index) {
            values.push_back("F" + std::to_string(index));
        }
        const std::array extras{
            "Minus", "Equals", "LBracket", "RBracket",
            "Semicolon", "Quote", "Comma", "Period", "Slash",
            "Backslash", "NumPad0", "NumPad1", "NumPad2",
            "NumPad3", "NumPad4", "NumPad5", "NumPad6",
            "NumPad7", "NumPad8", "NumPad9", "NumPadAdd",
            "NumPadSub", "NumPadMul", "NumPadDiv",
        };
        values.insert(values.end(), extras.begin(), extras.end());
        return values;
    }();
    return controls;
}

const std::vector<std::string>& ControlsForSource(
    INPUT::InputBindingSource source) {
    static const std::vector<std::string> mouseButtons{
        "Left", "Right", "Middle", "X1", "X2" };
    static const std::vector<std::string> mouseDeltaX{ "DeltaX" };
    static const std::vector<std::string> mouseDeltaY{ "DeltaY" };
    static const std::vector<std::string> mouseWheel{ "Wheel" };
    static const std::vector<std::string> gamepadButtons{
        "A", "B", "X", "Y", "LeftShoulder", "RightShoulder",
        "Back", "Start", "LeftStick", "RightStick", "DPadUp",
        "DPadDown", "DPadLeft", "DPadRight" };
    static const std::vector<std::string> gamepadAxes{
        "LeftX", "LeftY", "RightX", "RightY",
        "LeftTrigger", "RightTrigger" };
    switch (source) {
    case INPUT::InputBindingSource::Keyboard: return KeyboardControls();
    case INPUT::InputBindingSource::MouseButton: return mouseButtons;
    case INPUT::InputBindingSource::MouseDeltaX: return mouseDeltaX;
    case INPUT::InputBindingSource::MouseDeltaY: return mouseDeltaY;
    case INPUT::InputBindingSource::MouseWheel: return mouseWheel;
    case INPUT::InputBindingSource::GamepadButton: return gamepadButtons;
    case INPUT::InputBindingSource::GamepadAxis: return gamepadAxes;
    default: return KeyboardControls();
    }
}

} // namespace

bool EditString(const char* label, std::string& value, size_t capacity) {
#if defined(HIKARI_WITH_EDITOR)
    std::vector<char> buffer((std::max)(capacity, value.size() + 2), '\0');
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
    value = buffer.data();
    return true;
#else
    (void)label; (void)value; (void)capacity;
    return false;
#endif
}

bool MatchesFilter(std::string_view value, const char* filter) {
    if (filter == nullptr || *filter == '\0') return true;
    return TEXT::ToLowerAsciiCopy(value).find(TEXT::ToLowerAsciiCopy(filter)) != std::string::npos;
}

bool DrawValueTypeCombo(
    const char* label,
    INPUT::InputActionValueType& valueType) {
#if defined(HIKARI_WITH_EDITOR)
    bool changed = false;
    constexpr std::array types{
        INPUT::InputActionValueType::Button,
        INPUT::InputActionValueType::Axis1D,
        INPUT::InputActionValueType::Axis2D,
    };
    if (ImGui::BeginCombo(label, INPUT::ToString(valueType))) {
        for (INPUT::InputActionValueType type : types) {
            const bool selected = type == valueType;
            if (ImGui::Selectable(INPUT::ToString(type), selected)) {
                valueType = type;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
#else
    (void)label; (void)valueType;
    return false;
#endif
}

bool DrawBindingSourceCombo(
    const char* label,
    INPUT::InputBindingSource& source) {
#if defined(HIKARI_WITH_EDITOR)
    bool changed = false;
    constexpr std::array sources{
        INPUT::InputBindingSource::Keyboard,
        INPUT::InputBindingSource::MouseButton,
        INPUT::InputBindingSource::MouseDeltaX,
        INPUT::InputBindingSource::MouseDeltaY,
        INPUT::InputBindingSource::MouseWheel,
        INPUT::InputBindingSource::GamepadButton,
        INPUT::InputBindingSource::GamepadAxis,
    };
    if (ImGui::BeginCombo(label, INPUT::ToString(source))) {
        for (INPUT::InputBindingSource candidate : sources) {
            const bool selected = candidate == source;
            if (ImGui::Selectable(INPUT::ToString(candidate), selected)) {
                source = candidate;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
#else
    (void)label; (void)source;
    return false;
#endif
}

std::string DefaultControl(INPUT::InputBindingSource source) {
    const std::vector<std::string>& controls = ControlsForSource(source);
    return controls.empty() ? std::string{} : controls.front();
}

bool DrawControlPicker(
    const char* label,
    INPUT::InputBindingSource source,
    std::string& control) {
#if defined(HIKARI_WITH_EDITOR)
    bool changed = false;
    const std::vector<std::string>& options = ControlsForSource(source);
    if (ImGui::BeginCombo(label, control.empty() ? "Select control" : control.c_str())) {
        for (const std::string& candidate : options) {
            const bool selected = candidate == control;
            if (ImGui::Selectable(candidate.c_str(), selected)) {
                control = candidate;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
#else
    (void)label; (void)source; (void)control;
    return false;
#endif
}

bool DrawModifierPicker(std::string& modifier) {
#if defined(HIKARI_WITH_EDITOR)
    static constexpr std::array<const char*, 10> modifiers{
        "", "Shift", "Ctrl", "Alt", "Mouse:Left", "Mouse:Right",
        "Mouse:Middle", "Gamepad:LeftShoulder",
        "Gamepad:RightShoulder", "Gamepad:A" };
    const char* preview = modifier.empty() ? "None" : modifier.c_str();
    bool changed = false;
    if (ImGui::BeginCombo("Modifier", preview)) {
        for (const char* candidate : modifiers) {
            const bool selected = modifier == candidate;
            const char* display = *candidate == '\0' ? "None" : candidate;
            if (ImGui::Selectable(display, selected)) {
                modifier = candidate;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
#else
    (void)modifier;
    return false;
#endif
}

const char* DeviceLabel(INPUT::InputDeviceKind device) {
    switch (device) {
    case INPUT::InputDeviceKind::KeyboardMouse: return "Keyboard / Mouse";
    case INPUT::InputDeviceKind::Gamepad: return "Gamepad";
    default: return "None";
    }
}

} // namespace HIKARI::EDITOR::INPUT_WIDGETS
