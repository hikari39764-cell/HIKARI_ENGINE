#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "Input/Runtime/HIKARI_InputRebindOperation.h"
#include "Input/Runtime/HIKARI_InputTypes.h"

namespace HIKARI::INPUT {

class IInputBackend {
public:
    virtual ~IInputBackend() = default;

    virtual void SetHostWindow(void* nativeWindow) = 0;
    virtual void SetMouseCaptureMode(MouseCaptureMode mode) = 0;
    virtual void SetMouseCaptureRegion(const MouseCaptureRegion& region) = 0;
    virtual void ClearMouseCaptureRegion() = 0;
    virtual MouseCaptureMode GetMouseCaptureMode() const noexcept = 0;
    virtual void SetExternalMouseWheel(float delta) = 0;
    virtual void Reset() = 0;
    virtual void Poll(InputDeviceState& out) = 0;
    virtual float ReadControl(
        const InputDeviceState& state,
        InputBindingSource source,
        std::string_view control,
        uint32_t gamepadIndex) const = 0;
    virtual bool IsAnyControlActive(
        const InputDeviceState& state,
        float axisThreshold) const = 0;
    virtual void EnumerateNewControls(
        const InputDeviceState& previous,
        const InputDeviceState& current,
        float axisThreshold,
        bool includeMouseMotion,
        std::vector<InputControlActuation>& outCandidates) const = 0;
    virtual bool SetGamepadVibration(
        uint32_t gamepadIndex,
        float lowFrequency,
        float highFrequency) = 0;
};

} // namespace HIKARI::INPUT
