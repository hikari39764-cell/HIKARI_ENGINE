#include "Runtime/Presentation/HIKARI_GamePresentationService.h"
#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/WorldView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectActionValidation.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Runtime/Host/HIKARI_IEditorUiServices.h"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace HIKARI {
using namespace PROJECT::INTERNAL;
namespace {
GameProjectInputDevice ToProjectDevice(INPUT::InputDeviceKind device) noexcept {
    switch (device) {
    case INPUT::InputDeviceKind::KeyboardMouse: return GameProjectInputDevice::KeyboardMouse;
    case INPUT::InputDeviceKind::Gamepad: return GameProjectInputDevice::Gamepad;
    case INPUT::InputDeviceKind::None:
    default: return GameProjectInputDevice::None;
    }
}

bool ToProjectInputState(const INPUT::InputActionState& source, GameProjectInputActionState& outState) noexcept {
    if (!std::isfinite(source.x) || !std::isfinite(source.y) || !std::isfinite(source.heldSeconds) ||
        source.heldSeconds < 0.0f) {
        return false;
    }
    switch (source.valueType) {
    case INPUT::InputActionValueType::Button: outState.valueType = GameProjectInputValueType::Button; break;
    case INPUT::InputActionValueType::Axis1D: outState.valueType = GameProjectInputValueType::Axis1D; break;
    case INPUT::InputActionValueType::Axis2D: outState.valueType = GameProjectInputValueType::Axis2D; break;
    default: return false;
    }
    outState.x = source.x;
    outState.y = source.y;
    outState.heldSeconds = source.heldSeconds;
    outState.down = source.down ? 1u : 0u;
    outState.pressed = source.pressed ? 1u : 0u;
    outState.released = source.released ? 1u : 0u;
    return true;
}
} // namespace

InputView GameProjectSdkAccess::MakeInputView(World& world) noexcept { return InputView(&world); }

InputView WorldView::GetInput() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeInputView(*world) : InputView{};
}

bool InputView::GetAction(std::string_view actionId, GameProjectInputActionState& outState) const noexcept {
    outState = {};
    try {
        if (world_ == nullptr || actionId.empty()) {
            return false;
        }
        if (!SERVICES::IsStandaloneGameHost() && !SERVICES::IsInProcessGamePresentationActive()) {
            const auto* editor = RUNTIME::GetEditorUiServices();
            if (editor == nullptr || !editor->IsGameViewportPlayInputActive()) {
                return false;
            }
        }
        auto* world = static_cast<World*>(world_);
        INPUT::InputService* input = world->Services().Find<INPUT::InputService>();
        if (input == nullptr) {
            return false;
        }
        const INPUT::InputActionState* state = input->GetSnapshot().FindAction(std::string(actionId));
        return state != nullptr && ToProjectInputState(*state, outState);
    } catch (...) {
        outState = {};
        return false;
    }
}

GameProjectInputDevice InputView::GetLastActiveDevice() const noexcept {
    try {
        if (world_ == nullptr) {
            return GameProjectInputDevice::None;
        }
        auto* world = static_cast<World*>(world_);
        const INPUT::InputService* input = world->Services().Find<INPUT::InputService>();
        return input != nullptr ? ToProjectDevice(input->GetSnapshot().GetLastActiveDevice())
                                : GameProjectInputDevice::None;
    } catch (...) {
        return GameProjectInputDevice::None;
    }
}

uint64_t InputView::GetFrameIndex() const noexcept {
    try {
        if (world_ == nullptr) {
            return 0u;
        }
        auto* world = static_cast<World*>(world_);
        const INPUT::InputService* input = world->Services().Find<INPUT::InputService>();
        return input != nullptr ? input->GetSnapshot().GetFrameIndex() : 0u;
    } catch (...) {
        return 0u;
    }
}

bool InputView::GetPointerState(GameProjectPointerState& outState) const noexcept {
    outState = {};
    try {
        if (world_ == nullptr) {
            return false;
        }
        auto* world = static_cast<World*>(world_);
        const INPUT::InputService* input = world->Services().Find<INPUT::InputService>();
        uint32_t width = 0, height = 0;
        INPUT::InputPointerState pointer{};
        if (!input || !SERVICES::TryGetGamePointer(*input, pointer, width, height))
            return false;
        outState.x = pointer.x;
        outState.y = pointer.y;
        outState.normalizedX = pointer.normalizedX;
        outState.normalizedY = pointer.normalizedY;
        outState.primaryDown = pointer.primaryDown ? 1u : 0u;
        outState.secondaryDown = pointer.secondaryDown ? 1u : 0u;
        outState.valid = 1u;
        outState.logicalWidth = static_cast<uint32_t>(width);
        outState.logicalHeight = static_cast<uint32_t>(height);
        return true;
    } catch (...) {
        outState = {};
        return false;
    }
}

bool InputView::SetPointerCursorVisible(uint8_t visible) const noexcept {
    try {
        auto* world = static_cast<World*>(world_);
        if (!world || !world->IsOwnerThread() || visible > 1u)
            return false;
        auto* input = world->Services().Find<INPUT::InputService>();
        if (!input)
            return false;
        if (!visible) {
            const auto* camera = world->Services().Find<GameplayCameraService>();
            GameProjectPointerState pointer{};
            if (!camera || !camera->IsRuntimeCameraActive() || !GetPointerState(pointer) || !pointer.valid)
                return false;
        }
        input->SetPointerCursorVisible(visible != 0u);
        return true;
    } catch (...) {
        return false;
    }
}

bool InputView::SetContextActive(std::string_view contextId, uint8_t active) const noexcept {
    try {
        if (world_ == nullptr || contextId.empty()) {
            return false;
        }
        auto* world = static_cast<World*>(world_);
        INPUT::InputService* input = world->Services().Find<INPUT::InputService>();
        if (input == nullptr) {
            return false;
        }
        input->Contexts().SetActive(std::string(contextId), active != 0u);
        return true;
    } catch (...) {
        return false;
    }
}

bool InputView::SetUiInteractionActive(uint8_t active) const noexcept {
    try {
        if (world_ == nullptr) {
            return false;
        }
        auto* world = static_cast<World*>(world_);
        INPUT::InputService* input = world->Services().Find<INPUT::InputService>();
        if (input == nullptr) {
            return false;
        }
        input->SetUiInteractionActive(active != 0u);
        if (active != 0u) {
            input->SetMouseCaptureMode(INPUT::MouseCaptureMode::Free);
            input->ClearMouseCaptureRegion();
        } else {
            input->SetMouseCaptureMode(SERVICES::IsStandaloneGameHost() || SERVICES::IsInProcessGamePresentationActive()
                                           ? INPUT::MouseCaptureMode::Relative
                                           : INPUT::MouseCaptureMode::Free);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool InputView::SetGamepadVibration(float lowFrequency, float highFrequency, float durationSeconds) const noexcept {
    try {
        if (world_ == nullptr || !std::isfinite(lowFrequency) || !std::isfinite(highFrequency) ||
            !std::isfinite(durationSeconds) || durationSeconds < 0.0f) {
            return false;
        }
        auto* world = static_cast<World*>(world_);
        INPUT::InputService* input = world->Services().Find<INPUT::InputService>();
        return input != nullptr && input->SetGamepadVibration(std::clamp(lowFrequency, 0.0f, 1.0f),
                                                              std::clamp(highFrequency, 0.0f, 1.0f), durationSeconds);
    } catch (...) {
        return false;
    }
}

void InputView::StopGamepadVibration() const noexcept {
    try {
        if (world_ == nullptr) {
            return;
        }
        auto* world = static_cast<World*>(world_);
        if (INPUT::InputService* input = world->Services().Find<INPUT::InputService>()) {
            input->StopGamepadVibration();
        }
    } catch (...) {
    }
}
} // namespace HIKARI
