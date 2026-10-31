#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <tuple>

#include "Input/Platform/HIKARI_IInputBackend.h"

namespace HIKARI::INPUT {

class Win32InputBackend final : public IInputBackend {
public:
    ~Win32InputBackend() override;

    void SetHostWindow(void* nativeWindow) override;
    bool HasInputFocus() const noexcept override;
    void SetMouseCaptureMode(MouseCaptureMode mode) override;
    void SetMouseCaptureRegion(const MouseCaptureRegion& region) override;
    void ClearMouseCaptureRegion() override;
    MouseCaptureMode GetMouseCaptureMode() const noexcept override { return mouseCaptureMode_; }
    void SetExternalMouseWheel(float delta) override;
    void ResetTextInput() noexcept override;
    void SetImeCaretAnchor(ImeCaretAnchor anchor) noexcept override;
    void SetPointerCursorVisible(bool visible) override;
    void Reset() override;
    void Poll(InputDeviceState& out) override;
    float ReadControl(const InputDeviceState& state, InputBindingSource source, std::string_view control,
                      uint32_t gamepadIndex) const override;
    bool IsAnyControlActive(const InputDeviceState& state, float axisThreshold) const override;
    void EnumerateNewControls(const InputDeviceState& previous, const InputDeviceState& current, float axisThreshold,
                              bool includeMouseMotion,
                              std::vector<InputControlActuation>& outCandidates) const override;
    bool SetGamepadVibration(uint32_t gamepadIndex, float lowFrequency, float highFrequency) override;

private:
    void ApplyImeCaretAnchor() noexcept;
    void ReleaseRelativeMouseCapture() noexcept;

    void* hostWindow_ = nullptr;
    MouseCaptureMode mouseCaptureMode_ = MouseCaptureMode::Free;
    std::optional<MouseCaptureRegion> mouseCaptureRegion_{};
    float externalWheel_ = 0.0f;
    bool hasPreviousMouse_ = false;
    bool relativeMouseActive_ = false;
    bool relativeMousePrimed_ = false;
    bool cursorHiddenByBackend_ = false;
    bool pointerCursorHidden_ = false;
    ImeCaretAnchor pendingImeCaretAnchor_{};
    ImeCaretAnchor appliedImeCaretAnchor_{};
    bool hasPendingImeCaretAnchor_ = false;
    long previousMouseX_ = 0;
    long previousMouseY_ = 0;
    // 未接続の枠への XInputGetState は遅いので、次に探る時刻（GetTickCount64 の ms）まで飛ばす。接続中は 0（毎フレーム）。
    std::array<uint64_t, std::tuple_size_v<decltype(InputDeviceState::gamepads)>> nextGamepadProbeMs_{};
};

} // namespace HIKARI::INPUT
