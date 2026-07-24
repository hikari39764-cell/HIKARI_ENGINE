#include "Input/Runtime/HIKARI_InputRebindOperation.h"

#include <algorithm>

namespace HIKARI::INPUT {

void InputRebindOperation::Begin(const InputRebindOptions& options) {
    options_ = options;
    options_.axisThreshold = (std::clamp)(options_.axisThreshold, 0.05f, 1.0f);
    options_.releaseThreshold =
        (std::clamp)(options_.releaseThreshold, 0.01f, 1.0f);
    options_.timeoutSeconds = (std::max)(options_.timeoutSeconds, 0.1f);
    result_ = {};
    elapsedSeconds_ = 0.0f;
    status_ = InputRebindStatus::WaitingForRelease;
}

void InputRebindOperation::Update(
    float unscaledDeltaSeconds,
    bool allControlsReleased,
    const std::vector<InputControlActuation>& candidates) {
    if (!IsActive()) return;

    elapsedSeconds_ += (std::max)(unscaledDeltaSeconds, 0.0f);
    if (elapsedSeconds_ >= options_.timeoutSeconds) {
        status_ = InputRebindStatus::TimedOut;
        return;
    }

    const auto escape = std::find_if(
        candidates.begin(), candidates.end(),
        [](const InputControlActuation& candidate) {
            return candidate.source == InputBindingSource::Keyboard &&
                candidate.control == "Escape";
        });
    if (escape != candidates.end()) {
        status_ = InputRebindStatus::Cancelled;
        return;
    }

    if (status_ == InputRebindStatus::WaitingForRelease) {
        if (allControlsReleased) {
            status_ = InputRebindStatus::Listening;
        }
        return;
    }

    for (const InputControlActuation& candidate : candidates) {
        if (!IsAllowed(candidate.source) || candidate.control.empty()) continue;
        result_.source = candidate.source;
        result_.control = candidate.control;
        result_.actuation = candidate.value;
        status_ = InputRebindStatus::Captured;
        return;
    }
}

void InputRebindOperation::Cancel() {
    if (IsActive()) status_ = InputRebindStatus::Cancelled;
}

void InputRebindOperation::Reset() {
    options_ = {};
    result_ = {};
    status_ = InputRebindStatus::Idle;
    elapsedSeconds_ = 0.0f;
}

bool InputRebindOperation::ConsumeResult(InputRebindResult& outResult) {
    if (status_ != InputRebindStatus::Captured) return false;
    outResult = result_;
    Reset();
    return true;
}

float InputRebindOperation::GetRemainingSeconds() const noexcept {
    return (std::max)(options_.timeoutSeconds - elapsedSeconds_, 0.0f);
}

bool InputRebindOperation::IsActive() const noexcept {
    return status_ == InputRebindStatus::WaitingForRelease ||
        status_ == InputRebindStatus::Listening;
}

bool InputRebindOperation::IsAllowed(InputBindingSource source) const noexcept {
    switch (source) {
    case InputBindingSource::Keyboard: return options_.allowKeyboard;
    case InputBindingSource::MouseButton: return options_.allowMouseButtons;
    case InputBindingSource::MouseDeltaX:
    case InputBindingSource::MouseDeltaY: return options_.allowMouseMotion;
    case InputBindingSource::MouseWheel: return options_.allowMouseWheel;
    case InputBindingSource::GamepadButton: return options_.allowGamepadButtons;
    case InputBindingSource::GamepadAxis: return options_.allowGamepadAxes;
    default: return false;
    }
}

const char* ToString(InputRebindStatus status) noexcept {
    switch (status) {
    case InputRebindStatus::Idle: return "Idle";
    case InputRebindStatus::WaitingForRelease: return "Waiting for release";
    case InputRebindStatus::Listening: return "Listening";
    case InputRebindStatus::Captured: return "Captured";
    case InputRebindStatus::Cancelled: return "Cancelled";
    case InputRebindStatus::TimedOut: return "Timed out";
    default: return "Idle";
    }
}

} // namespace HIKARI::INPUT
