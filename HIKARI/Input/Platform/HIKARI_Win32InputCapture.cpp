#include "Input/Platform/HIKARI_Win32InputBackend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

#include <Windows.h>
#include <Xinput.h>

namespace HIKARI::INPUT {
namespace {

std::string VirtualKeyName(int vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        return std::string(1, static_cast<char>(vk));
    }
    switch (vk) {
    case VK_SPACE: return "Space";
    case VK_RETURN: return "Enter";
    case VK_ESCAPE: return "Escape";
    case VK_TAB: return "Tab";
    case VK_BACK: return "Backspace";
    case VK_LEFT: return "Left";
    case VK_RIGHT: return "Right";
    case VK_UP: return "Up";
    case VK_DOWN: return "Down";
    case VK_SHIFT: return "Shift";
    case VK_CONTROL: return "Ctrl";
    case VK_MENU: return "Alt";
    case VK_HOME: return "Home";
    case VK_END: return "End";
    case VK_DELETE: return "Delete";
    case VK_INSERT: return "Insert";
    case VK_PRIOR: return "PageUp";
    case VK_NEXT: return "PageDown";
    case VK_OEM_MINUS: return "Minus";
    case VK_OEM_PLUS: return "Equals";
    case VK_OEM_4: return "LBracket";
    case VK_OEM_6: return "RBracket";
    case VK_OEM_1: return "Semicolon";
    case VK_OEM_7: return "Quote";
    case VK_OEM_COMMA: return "Comma";
    case VK_OEM_PERIOD: return "Period";
    case VK_OEM_2: return "Slash";
    case VK_OEM_5: return "Backslash";
    default: break;
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        return "F" + std::to_string(vk - VK_F1 + 1);
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        return "NumPad" + std::to_string(vk - VK_NUMPAD0);
    }
    switch (vk) {
    case VK_ADD: return "NumPadAdd";
    case VK_SUBTRACT: return "NumPadSub";
    case VK_MULTIPLY: return "NumPadMul";
    case VK_DIVIDE: return "NumPadDiv";
    default: return {};
    }
}

} // namespace

bool Win32InputBackend::IsAnyControlActive(
    const InputDeviceState& state,
    float axisThreshold) const {
    if (std::any_of(state.keyboard.begin(), state.keyboard.end(),
            [](uint8_t value) { return value != 0; }) ||
        std::any_of(state.mouseButtons.begin(), state.mouseButtons.end(),
            [](uint8_t value) { return value != 0; })) {
        return true;
    }
    for (const GamepadState& gamepad : state.gamepads) {
        if (!gamepad.connected) continue;
        if (gamepad.buttons != 0 ||
            std::abs(gamepad.leftX) >= axisThreshold ||
            std::abs(gamepad.leftY) >= axisThreshold ||
            std::abs(gamepad.rightX) >= axisThreshold ||
            std::abs(gamepad.rightY) >= axisThreshold ||
            gamepad.leftTrigger >= axisThreshold ||
            gamepad.rightTrigger >= axisThreshold) {
            return true;
        }
    }
    return false;
}

void Win32InputBackend::EnumerateNewControls(
    const InputDeviceState& previous,
    const InputDeviceState& current,
    float axisThreshold,
    bool includeMouseMotion,
    std::vector<InputControlActuation>& outCandidates) const {
    outCandidates.clear();
    for (int vk = 1; vk < 256; ++vk) {
        if (current.keyboard[vk] == 0 || previous.keyboard[vk] != 0) continue;
        std::string name = VirtualKeyName(vk);
        if (!name.empty()) {
            outCandidates.push_back(InputControlActuation{
                InputBindingSource::Keyboard, std::move(name), 1.0f });
        }
    }

    static constexpr std::array<const char*, 5> mouseButtonNames{
        "Left", "Right", "Middle", "X1", "X2" };
    for (size_t index = 0; index < current.mouseButtons.size(); ++index) {
        if (current.mouseButtons[index] != 0 &&
            previous.mouseButtons[index] == 0) {
            outCandidates.push_back(InputControlActuation{
                InputBindingSource::MouseButton,
                mouseButtonNames[index], 1.0f });
        }
    }
    if (std::abs(current.mouseWheel) > 0.0f) {
        outCandidates.push_back(InputControlActuation{
            InputBindingSource::MouseWheel, "Wheel", current.mouseWheel });
    }
    if (includeMouseMotion) {
        constexpr float threshold = 2.0f;
        if (std::abs(current.mouseDeltaX) >= threshold) {
            outCandidates.push_back(InputControlActuation{
                InputBindingSource::MouseDeltaX,
                "DeltaX", current.mouseDeltaX });
        }
        if (std::abs(current.mouseDeltaY) >= threshold) {
            outCandidates.push_back(InputControlActuation{
                InputBindingSource::MouseDeltaY,
                "DeltaY", current.mouseDeltaY });
        }
    }

    static constexpr std::array<std::pair<int, const char*>, 14>
        gamepadButtons{{
            { XINPUT_GAMEPAD_A, "A" },
            { XINPUT_GAMEPAD_B, "B" },
            { XINPUT_GAMEPAD_X, "X" },
            { XINPUT_GAMEPAD_Y, "Y" },
            { XINPUT_GAMEPAD_LEFT_SHOULDER, "LeftShoulder" },
            { XINPUT_GAMEPAD_RIGHT_SHOULDER, "RightShoulder" },
            { XINPUT_GAMEPAD_BACK, "Back" },
            { XINPUT_GAMEPAD_START, "Start" },
            { XINPUT_GAMEPAD_LEFT_THUMB, "LeftStick" },
            { XINPUT_GAMEPAD_RIGHT_THUMB, "RightStick" },
            { XINPUT_GAMEPAD_DPAD_UP, "DPadUp" },
            { XINPUT_GAMEPAD_DPAD_DOWN, "DPadDown" },
            { XINPUT_GAMEPAD_DPAD_LEFT, "DPadLeft" },
            { XINPUT_GAMEPAD_DPAD_RIGHT, "DPadRight" },
        }};
    for (size_t padIndex = 0; padIndex < current.gamepads.size(); ++padIndex) {
        const GamepadState& oldPad = previous.gamepads[padIndex];
        const GamepadState& pad = current.gamepads[padIndex];
        if (!pad.connected) continue;
        for (const auto& [mask, name] : gamepadButtons) {
            if ((pad.buttons & mask) != 0 && (oldPad.buttons & mask) == 0) {
                outCandidates.push_back(InputControlActuation{
                    InputBindingSource::GamepadButton, name, 1.0f });
            }
        }

        const std::array<std::pair<const char*, float>, 6> axes{{
            { "LeftX", pad.leftX }, { "LeftY", pad.leftY },
            { "RightX", pad.rightX }, { "RightY", pad.rightY },
            { "LeftTrigger", pad.leftTrigger },
            { "RightTrigger", pad.rightTrigger },
        }};
        const std::array<float, 6> oldAxes{
            oldPad.leftX, oldPad.leftY, oldPad.rightX, oldPad.rightY,
            oldPad.leftTrigger, oldPad.rightTrigger };
        for (size_t axisIndex = 0; axisIndex < axes.size(); ++axisIndex) {
            const float value = axes[axisIndex].second;
            if (std::abs(value) >= axisThreshold &&
                std::abs(oldAxes[axisIndex]) < axisThreshold) {
                outCandidates.push_back(InputControlActuation{
                    InputBindingSource::GamepadAxis,
                    axes[axisIndex].first, value });
            }
        }
    }
}

} // namespace HIKARI::INPUT
