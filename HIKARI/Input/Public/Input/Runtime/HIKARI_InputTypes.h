#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Core/Text/HIKARI_EnumNames.h"
#include "Platform/HIKARI_TextInputTypes.h"

namespace HIKARI::INPUT {

enum class InputDeviceKind {
    None,
    KeyboardMouse,
    Gamepad,
};

// The presentation layer chooses how the operating-system cursor is owned.
// Gameplay systems consume relative mouse actions and do not manage the cursor.
enum class MouseCaptureMode {
    Free,
    Relative,
};

// Optional screen-space confinement for relative mouse ownership. Presentation
// code owns this rectangle; gameplay remains unaware of editor/window layout.
struct MouseCaptureRegion {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;

    bool IsValid() const noexcept { return right > left && bottom > top; }
};

enum class InputActionValueType {
    Button,
    Axis1D,
    Axis2D,
};

enum class InputBindingSource {
    Keyboard,
    MouseButton,
    MouseDeltaX,
    MouseDeltaY,
    MouseWheel,
    GamepadButton,
    GamepadAxis,
};

inline constexpr TEXT::EnumName<InputActionValueType> kInputActionValueTypeNames[] = {
    {InputActionValueType::Button, "Button"},
    {InputActionValueType::Axis1D, "Axis1D"},
    {InputActionValueType::Axis2D, "Axis2D"},
};

inline constexpr TEXT::EnumName<InputBindingSource> kInputBindingSourceNames[] = {
    {InputBindingSource::Keyboard, "Keyboard"},       {InputBindingSource::MouseButton, "MouseButton"},
    {InputBindingSource::MouseDeltaX, "MouseDeltaX"}, {InputBindingSource::MouseDeltaY, "MouseDeltaY"},
    {InputBindingSource::MouseWheel, "MouseWheel"},   {InputBindingSource::GamepadButton, "GamepadButton"},
    {InputBindingSource::GamepadAxis, "GamepadAxis"},
};

struct GamepadState {
    bool connected = false;
    uint16_t buttons = 0;
    float leftX = 0.0f;
    float leftY = 0.0f;
    float rightX = 0.0f;
    float rightY = 0.0f;
    float leftTrigger = 0.0f;
    float rightTrigger = 0.0f;
};

struct InputDeviceState {
    std::array<uint8_t, 256> keyboard{};
    std::array<uint8_t, 5> mouseButtons{};
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    float mouseWheel = 0.0f;
    std::array<GamepadState, 4> gamepads{};
    InputDeviceKind lastActiveDevice = InputDeviceKind::None;
};

struct InputPointerState {
    float x = 0.0f;
    float y = 0.0f;
    float normalizedX = 0.0f;
    float normalizedY = 0.0f;
    bool primaryDown = false;
    bool secondaryDown = false;
};

struct InputActionState {
    InputActionValueType valueType = InputActionValueType::Button;
    float x = 0.0f;
    float y = 0.0f;
    bool down = false;
    bool pressed = false;
    bool released = false;
    float heldSeconds = 0.0f;
};

class InputSnapshot {
public:
    const InputActionState* FindAction(const std::string& actionId) const;
    bool IsDown(const std::string& actionId) const;
    bool IsPressed(const std::string& actionId) const;
    float GetAxis1D(const std::string& actionId) const;
    std::array<float, 2> GetAxis2D(const std::string& actionId) const;
    bool TryGetPointerState(uint32_t logicalWidth, uint32_t logicalHeight, InputPointerState& outState) const noexcept;

    const InputDeviceState& GetDevices() const noexcept { return devices_; }
    // Events are owned by this snapshot for one InputService::Update only.
    const std::vector<TextInputEvent>& GetTextInputEvents() const noexcept { return textInputEvents_; }
    InputDeviceKind GetLastActiveDevice() const noexcept { return devices_.lastActiveDevice; }
    uint64_t GetFrameIndex() const noexcept { return frameIndex_; }

private:
    friend class InputService;
    uint64_t frameIndex_ = 0;
    InputDeviceState devices_{};
    std::unordered_map<std::string, InputActionState> actions_{};
    std::vector<TextInputEvent> textInputEvents_{};
};

} // namespace HIKARI::INPUT
