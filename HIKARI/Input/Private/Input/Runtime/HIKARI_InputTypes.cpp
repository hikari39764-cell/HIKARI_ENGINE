#include "Input/Runtime/HIKARI_InputTypes.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::INPUT {

const InputActionState* InputSnapshot::FindAction(const std::string& actionId) const {
    const auto it = actions_.find(actionId);
    return it != actions_.end() ? &it->second : nullptr;
}

bool InputSnapshot::IsDown(const std::string& actionId) const {
    const InputActionState* state = FindAction(actionId);
    return state != nullptr && state->down;
}

bool InputSnapshot::IsPressed(const std::string& actionId) const {
    const InputActionState* state = FindAction(actionId);
    return state != nullptr && state->pressed;
}

float InputSnapshot::GetAxis1D(const std::string& actionId) const {
    const InputActionState* state = FindAction(actionId);
    return state != nullptr ? state->x : 0.0f;
}

std::array<float, 2> InputSnapshot::GetAxis2D(const std::string& actionId) const {
    const InputActionState* state = FindAction(actionId);
    return state != nullptr ? std::array<float, 2>{state->x, state->y} : std::array<float, 2>{0.0f, 0.0f};
}

bool InputSnapshot::TryGetPointerState(uint32_t logicalWidth, uint32_t logicalHeight,
                                       InputPointerState& outState) const noexcept {
    outState = {};
    if (logicalWidth == 0u || logicalHeight == 0u || !std::isfinite(devices_.mouseX) ||
        !std::isfinite(devices_.mouseY)) {
        return false;
    }
    outState.x = devices_.mouseX;
    outState.y = devices_.mouseY;
    outState.normalizedX = std::clamp(devices_.mouseX / static_cast<float>(logicalWidth), 0.0f, 1.0f);
    outState.normalizedY = std::clamp(devices_.mouseY / static_cast<float>(logicalHeight), 0.0f, 1.0f);
    outState.primaryDown = devices_.mouseButtons[0] != 0u;
    outState.secondaryDown = devices_.mouseButtons[1] != 0u;
    return true;
}

} // namespace HIKARI::INPUT
