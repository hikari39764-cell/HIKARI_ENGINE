#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI::INPUT {

const InputActionState* InputSnapshot::FindAction(
    const std::string& actionId) const {
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

bool InputSnapshot::IsReleased(const std::string& actionId) const {
    const InputActionState* state = FindAction(actionId);
    return state != nullptr && state->released;
}

float InputSnapshot::GetAxis1D(const std::string& actionId) const {
    const InputActionState* state = FindAction(actionId);
    return state != nullptr ? state->x : 0.0f;
}

std::array<float, 2> InputSnapshot::GetAxis2D(
    const std::string& actionId) const {
    const InputActionState* state = FindAction(actionId);
    return state != nullptr
        ? std::array<float, 2>{ state->x, state->y }
        : std::array<float, 2>{ 0.0f, 0.0f };
}

} // namespace HIKARI::INPUT
