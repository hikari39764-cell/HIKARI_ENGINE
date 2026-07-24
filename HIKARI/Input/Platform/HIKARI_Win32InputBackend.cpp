#include "Input/Platform/HIKARI_Win32InputBackend.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <Windows.h>
#include <Xinput.h>
#pragma comment(lib, "xinput9_1_0.lib")
#undef min
#undef max

namespace HIKARI::INPUT {
namespace {

int VirtualKeyFromName(std::string_view name) {
    if (name.size() == 1) {
        char value = name.front();
        if (value >= 'a' && value <= 'z') {
            value = static_cast<char>(value - 'a' + 'A');
        }
        if ((value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9')) {
            return static_cast<unsigned char>(value);
        }
    }
    if (name == "Space") return VK_SPACE;
    if (name == "Enter") return VK_RETURN;
    if (name == "Escape" || name == "Esc") return VK_ESCAPE;
    if (name == "Tab") return VK_TAB;
    if (name == "Backspace") return VK_BACK;
    if (name == "Left") return VK_LEFT;
    if (name == "Right") return VK_RIGHT;
    if (name == "Up") return VK_UP;
    if (name == "Down") return VK_DOWN;
    if (name == "Shift") return VK_SHIFT;
    if (name == "LeftShift") return VK_LSHIFT;
    if (name == "RightShift") return VK_RSHIFT;
    if (name == "Ctrl") return VK_CONTROL;
    if (name == "LeftCtrl") return VK_LCONTROL;
    if (name == "RightCtrl") return VK_RCONTROL;
    if (name == "Alt") return VK_MENU;
    if (name == "LeftAlt") return VK_LMENU;
    if (name == "RightAlt") return VK_RMENU;
    if (name == "Home") return VK_HOME;
    if (name == "End") return VK_END;
    if (name == "Delete") return VK_DELETE;
    if (name == "Insert") return VK_INSERT;
    if (name == "PageUp") return VK_PRIOR;
    if (name == "PageDown") return VK_NEXT;
    if (name == "Minus") return VK_OEM_MINUS;
    if (name == "Equals") return VK_OEM_PLUS;
    if (name == "LBracket") return VK_OEM_4;
    if (name == "RBracket") return VK_OEM_6;
    if (name == "Semicolon") return VK_OEM_1;
    if (name == "Quote") return VK_OEM_7;
    if (name == "Comma") return VK_OEM_COMMA;
    if (name == "Period") return VK_OEM_PERIOD;
    if (name == "Slash") return VK_OEM_2;
    if (name == "Backslash") return VK_OEM_5;
    if (name.size() >= 2 && name.front() == 'F') {
        int number = 0;
        for (size_t index = 1; index < name.size(); ++index) {
            if (name[index] < '0' || name[index] > '9') return 0;
            number = number * 10 + (name[index] - '0');
        }
        if (number >= 1 && number <= 24) return VK_F1 + number - 1;
    }
    if (name.rfind("NumPad", 0) == 0) {
        if (name.size() == 7 && name[6] >= '0' && name[6] <= '9') {
            return VK_NUMPAD0 + (name[6] - '0');
        }
        if (name == "NumPadAdd") return VK_ADD;
        if (name == "NumPadSub") return VK_SUBTRACT;
        if (name == "NumPadMul") return VK_MULTIPLY;
        if (name == "NumPadDiv") return VK_DIVIDE;
    }
    return 0;
}

int MouseButtonIndex(std::string_view name) {
    if (name == "Left") return 0;
    if (name == "Right") return 1;
    if (name == "Middle") return 2;
    if (name == "X1") return 3;
    if (name == "X2") return 4;
    return -1;
}

uint16_t GamepadButtonMask(std::string_view name) {
    if (name == "A") return XINPUT_GAMEPAD_A;
    if (name == "B") return XINPUT_GAMEPAD_B;
    if (name == "X") return XINPUT_GAMEPAD_X;
    if (name == "Y") return XINPUT_GAMEPAD_Y;
    if (name == "LeftShoulder") return XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (name == "RightShoulder") return XINPUT_GAMEPAD_RIGHT_SHOULDER;
    if (name == "Back") return XINPUT_GAMEPAD_BACK;
    if (name == "Start") return XINPUT_GAMEPAD_START;
    if (name == "LeftStick") return XINPUT_GAMEPAD_LEFT_THUMB;
    if (name == "RightStick") return XINPUT_GAMEPAD_RIGHT_THUMB;
    if (name == "DPadUp") return XINPUT_GAMEPAD_DPAD_UP;
    if (name == "DPadDown") return XINPUT_GAMEPAD_DPAD_DOWN;
    if (name == "DPadLeft") return XINPUT_GAMEPAD_DPAD_LEFT;
    if (name == "DPadRight") return XINPUT_GAMEPAD_DPAD_RIGHT;
    return 0;
}

float NormalizeThumb(short value) {
    return value < 0
        ? static_cast<float>(value) / 32768.0f
        : static_cast<float>(value) / 32767.0f;
}

bool IsHostActive(HWND host) {
    if (host == nullptr) return false;
    const HWND foreground = ::GetForegroundWindow();
    return foreground == host || ::IsChild(host, foreground) != FALSE;
}

} // namespace

Win32InputBackend::~Win32InputBackend() {
    ReleaseRelativeMouseCapture();
}

void Win32InputBackend::SetHostWindow(void* nativeWindow) {
    if (hostWindow_ != nativeWindow) {
        ReleaseRelativeMouseCapture();
        hostWindow_ = nativeWindow;
        hasPreviousMouse_ = false;
        relativeMousePrimed_ = false;
    }
}

void Win32InputBackend::SetMouseCaptureMode(MouseCaptureMode mode) {
    if (mouseCaptureMode_ == mode) return;
    ReleaseRelativeMouseCapture();
    mouseCaptureMode_ = mode;
    hasPreviousMouse_ = false;
    relativeMousePrimed_ = false;
}

void Win32InputBackend::SetMouseCaptureRegion(
    const MouseCaptureRegion& region) {
    if (!region.IsValid()) {
        ClearMouseCaptureRegion();
        return;
    }
    if (mouseCaptureRegion_ &&
        mouseCaptureRegion_->left == region.left &&
        mouseCaptureRegion_->top == region.top &&
        mouseCaptureRegion_->right == region.right &&
        mouseCaptureRegion_->bottom == region.bottom) {
        return;
    }
    ReleaseRelativeMouseCapture();
    mouseCaptureRegion_ = region;
}

void Win32InputBackend::ClearMouseCaptureRegion() {
    if (!mouseCaptureRegion_) return;
    ReleaseRelativeMouseCapture();
    mouseCaptureRegion_.reset();
}

void Win32InputBackend::SetExternalMouseWheel(float delta) {
    externalWheel_ += delta;
}

void Win32InputBackend::Reset() {
    ReleaseRelativeMouseCapture();
    mouseCaptureMode_ = MouseCaptureMode::Free;
    mouseCaptureRegion_.reset();
    externalWheel_ = 0.0f;
    hasPreviousMouse_ = false;
    previousMouseX_ = 0;
    previousMouseY_ = 0;
}

void Win32InputBackend::ReleaseRelativeMouseCapture() noexcept {
    if (relativeMouseActive_) {
        (void)::ClipCursor(nullptr);
        relativeMouseActive_ = false;
    }
    if (cursorHiddenByBackend_) {
        (void)::ShowCursor(TRUE);
        cursorHiddenByBackend_ = false;
    }
    relativeMousePrimed_ = false;
    hasPreviousMouse_ = false;
}

void Win32InputBackend::Poll(InputDeviceState& out) {
    out = {};
    const HWND host = static_cast<HWND>(hostWindow_);
    const bool active = IsHostActive(host);
    if (active) {
        for (int vk = 1; vk < 256; ++vk) {
            out.keyboard[static_cast<size_t>(vk)] =
                (::GetAsyncKeyState(vk) & 0x8000) != 0 ? 1u : 0u;
        }
        out.mouseButtons[0] = (::GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        out.mouseButtons[1] = (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        out.mouseButtons[2] = (::GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        out.mouseButtons[3] = (::GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0;
        out.mouseButtons[4] = (::GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;

        POINT cursor{};
        if (::GetCursorPos(&cursor) != FALSE) {
            if (mouseCaptureMode_ == MouseCaptureMode::Relative &&
                host != nullptr) {
                RECT clientRect{};
                if (::GetClientRect(host, &clientRect) != FALSE &&
                    clientRect.right > clientRect.left &&
                    clientRect.bottom > clientRect.top) {
                    POINT corners[2] = {
                        { clientRect.left, clientRect.top },
                        { clientRect.right, clientRect.bottom },
                    };
                    (void)::MapWindowPoints(
                        host, nullptr, corners, 2);
                    RECT hostClipRect{
                        corners[0].x,
                        corners[0].y,
                        corners[1].x,
                        corners[1].y,
                    };
                    RECT clipRect = hostClipRect;
                    if (mouseCaptureRegion_) {
                        const RECT requested{
                            mouseCaptureRegion_->left,
                            mouseCaptureRegion_->top,
                            mouseCaptureRegion_->right,
                            mouseCaptureRegion_->bottom,
                        };
                        if (::IntersectRect(
                                &clipRect,
                                &hostClipRect,
                                &requested) == FALSE) {
                            ReleaseRelativeMouseCapture();
                            externalWheel_ = 0.0f;
                            return;
                        }
                    }
                    POINT center{
                        (clipRect.left + clipRect.right) / 2,
                        (clipRect.top + clipRect.bottom) / 2,
                    };

                    if (!relativeMouseActive_) {
                        (void)::ClipCursor(&clipRect);
                        relativeMouseActive_ = true;
                        if (!cursorHiddenByBackend_) {
                            (void)::ShowCursor(FALSE);
                            cursorHiddenByBackend_ = true;
                        }
                        relativeMousePrimed_ = false;
                    } else {
                        // Keep the confinement rectangle synchronized with a
                        // resizable or moving game window.
                        (void)::ClipCursor(&clipRect);
                    }

                    POINT clientCenter = center;
                    (void)::ScreenToClient(host, &clientCenter);
                    out.mouseX = static_cast<float>(clientCenter.x);
                    out.mouseY = static_cast<float>(clientCenter.y);
                    if (relativeMousePrimed_) {
                        out.mouseDeltaX = static_cast<float>(cursor.x - center.x);
                        out.mouseDeltaY = static_cast<float>(cursor.y - center.y);
                    }
                    (void)::SetCursorPos(center.x, center.y);
                    relativeMousePrimed_ = true;
                    previousMouseX_ = center.x;
                    previousMouseY_ = center.y;
                    hasPreviousMouse_ = true;
                } else {
                    ReleaseRelativeMouseCapture();
                }
            } else {
                POINT client = cursor;
                if (host != nullptr) {
                    (void)::ScreenToClient(host, &client);
                }
                out.mouseX = static_cast<float>(client.x);
                out.mouseY = static_cast<float>(client.y);
                if (hasPreviousMouse_) {
                    out.mouseDeltaX = static_cast<float>(cursor.x - previousMouseX_);
                    out.mouseDeltaY = static_cast<float>(cursor.y - previousMouseY_);
                }
                previousMouseX_ = cursor.x;
                previousMouseY_ = cursor.y;
                hasPreviousMouse_ = true;
            }
        }
        out.mouseWheel = externalWheel_;
    } else {
        ReleaseRelativeMouseCapture();
    }
    externalWheel_ = 0.0f;

    bool keyboardMouseActive = std::abs(out.mouseDeltaX) > 0.0f ||
        std::abs(out.mouseDeltaY) > 0.0f || std::abs(out.mouseWheel) > 0.0f;
    keyboardMouseActive = keyboardMouseActive ||
        std::any_of(out.keyboard.begin(), out.keyboard.end(),
            [](uint8_t value) { return value != 0; }) ||
        std::any_of(out.mouseButtons.begin(), out.mouseButtons.end(),
            [](uint8_t value) { return value != 0; });

    bool gamepadActive = false;
    for (DWORD index = 0; index < out.gamepads.size(); ++index) {
        XINPUT_STATE state{};
        if (::XInputGetState(index, &state) != ERROR_SUCCESS) continue;
        GamepadState& gamepad = out.gamepads[index];
        gamepad.connected = true;
        gamepad.buttons = state.Gamepad.wButtons;
        gamepad.leftX = NormalizeThumb(state.Gamepad.sThumbLX);
        gamepad.leftY = NormalizeThumb(state.Gamepad.sThumbLY);
        gamepad.rightX = NormalizeThumb(state.Gamepad.sThumbRX);
        gamepad.rightY = NormalizeThumb(state.Gamepad.sThumbRY);
        gamepad.leftTrigger = state.Gamepad.bLeftTrigger / 255.0f;
        gamepad.rightTrigger = state.Gamepad.bRightTrigger / 255.0f;
        gamepadActive = gamepadActive || gamepad.buttons != 0 ||
            std::abs(gamepad.leftX) > 0.15f ||
            std::abs(gamepad.leftY) > 0.15f ||
            std::abs(gamepad.rightX) > 0.15f ||
            std::abs(gamepad.rightY) > 0.15f ||
            gamepad.leftTrigger > 0.05f || gamepad.rightTrigger > 0.05f;
    }
    out.lastActiveDevice = gamepadActive
        ? InputDeviceKind::Gamepad
        : (keyboardMouseActive ? InputDeviceKind::KeyboardMouse
                               : InputDeviceKind::None);
}

float Win32InputBackend::ReadControl(
    const InputDeviceState& state,
    InputBindingSource source,
    std::string_view control,
    uint32_t gamepadIndex) const {
    switch (source) {
    case InputBindingSource::Keyboard: {
        const int vk = VirtualKeyFromName(control);
        return vk > 0 && vk < 256 ? static_cast<float>(state.keyboard[vk]) : 0.0f;
    }
    case InputBindingSource::MouseButton: {
        const int index = MouseButtonIndex(control);
        return index >= 0 ? static_cast<float>(state.mouseButtons[index]) : 0.0f;
    }
    case InputBindingSource::MouseDeltaX:
        return state.mouseDeltaX;
    case InputBindingSource::MouseDeltaY:
        return state.mouseDeltaY;
    case InputBindingSource::MouseWheel:
        return state.mouseWheel;
    case InputBindingSource::GamepadButton: {
        if (gamepadIndex >= state.gamepads.size()) return 0.0f;
        const uint16_t mask = GamepadButtonMask(control);
        return mask != 0 && (state.gamepads[gamepadIndex].buttons & mask) != 0
            ? 1.0f : 0.0f;
    }
    case InputBindingSource::GamepadAxis: {
        if (gamepadIndex >= state.gamepads.size()) return 0.0f;
        const GamepadState& pad = state.gamepads[gamepadIndex];
        if (control == "LeftX") return pad.leftX;
        if (control == "LeftY") return pad.leftY;
        if (control == "RightX") return pad.rightX;
        if (control == "RightY") return pad.rightY;
        if (control == "LeftTrigger") return pad.leftTrigger;
        if (control == "RightTrigger") return pad.rightTrigger;
        return 0.0f;
    }
    default:
        return 0.0f;
    }
}

bool Win32InputBackend::SetGamepadVibration(
    uint32_t gamepadIndex,
    float lowFrequency,
    float highFrequency) {
    if (gamepadIndex >= XUSER_MAX_COUNT) return false;
    XINPUT_VIBRATION vibration{};
    vibration.wLeftMotorSpeed = static_cast<WORD>(
        (std::clamp)(lowFrequency, 0.0f, 1.0f) * 65535.0f);
    vibration.wRightMotorSpeed = static_cast<WORD>(
        (std::clamp)(highFrequency, 0.0f, 1.0f) * 65535.0f);
    return ::XInputSetState(gamepadIndex, &vibration) == ERROR_SUCCESS;
}

} // namespace HIKARI::INPUT
