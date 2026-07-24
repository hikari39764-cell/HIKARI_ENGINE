#pragma once

#include <string>
#include <vector>

#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI::INPUT {

enum class InputRebindStatus {
    Idle,
    WaitingForRelease,
    Listening,
    Captured,
    Cancelled,
    TimedOut,
};

struct InputControlActuation {
    InputBindingSource source = InputBindingSource::Keyboard;
    std::string control{};
    float value = 0.0f;
};

struct InputRebindOptions {
    bool allowKeyboard = true;
    bool allowMouseButtons = true;
    bool allowMouseWheel = true;
    bool allowMouseMotion = false;
    bool allowGamepadButtons = true;
    bool allowGamepadAxes = true;
    bool suppressMappedActions = true;
    float axisThreshold = 0.65f;
    float releaseThreshold = 0.25f;
    float timeoutSeconds = 8.0f;
};

struct InputRebindResult {
    InputBindingSource source = InputBindingSource::Keyboard;
    std::string control{};
    float actuation = 0.0f;
};

class InputRebindOperation {
public:
    void Begin(const InputRebindOptions& options = {});
    void Update(
        float unscaledDeltaSeconds,
        bool allControlsReleased,
        const std::vector<InputControlActuation>& candidates);
    void Cancel();
    void Reset();
    bool ConsumeResult(InputRebindResult& outResult);

    InputRebindStatus GetStatus() const noexcept { return status_; }
    const InputRebindOptions& GetOptions() const noexcept { return options_; }
    float GetElapsedSeconds() const noexcept { return elapsedSeconds_; }
    float GetRemainingSeconds() const noexcept;
    bool IsActive() const noexcept;

private:
    bool IsAllowed(InputBindingSource source) const noexcept;

    InputRebindOptions options_{};
    InputRebindResult result_{};
    InputRebindStatus status_ = InputRebindStatus::Idle;
    float elapsedSeconds_ = 0.0f;
};

const char* ToString(InputRebindStatus status) noexcept;

} // namespace HIKARI::INPUT
