#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "Input/Assets/HIKARI_InputActionMap.h"
#include "Input/Runtime/HIKARI_InputContextStack.h"
#include "Input/Runtime/HIKARI_InputRebindOperation.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
#include "Input/Runtime/HIKARI_InputUser.h"

namespace HIKARI::INPUT {

class IInputBackend;

class InputService {
public:
    InputService();
    ~InputService();
    InputService(const InputService&) = delete;
    InputService& operator=(const InputService&) = delete;

    bool Initialize(const std::filesystem::path& projectRoot);
    bool InitializeTransient();
    void Shutdown();
    void SetBackend(std::unique_ptr<IInputBackend> backend);
    void SetHostWindow(void* nativeWindow);
    void* GetHostWindow() const noexcept { return hostWindow_; }
    void SetMouseCaptureMode(MouseCaptureMode mode);
    void SetMouseCaptureRegion(const MouseCaptureRegion& region);
    void ClearMouseCaptureRegion();
    MouseCaptureMode GetMouseCaptureMode() const noexcept;
    void SetUiInteractionActive(bool active) noexcept;
    bool IsUiInteractionActive() const noexcept { return uiInteractionActive_ || developerInputCaptured_; }
    void SetDeveloperInputCaptured(bool active) noexcept;
    bool IsDeveloperInputCaptured() const noexcept { return developerInputCaptured_; }
    void SetPointerCursorVisible(bool visible) noexcept { pointerCursorHiddenRequested_ = !visible; }
    void ApplyPointerCursorVisibility(bool pointerOwned);
    void SetExternalMouseWheel(float delta);
    void SetExternalTextInputEvents(std::vector<TextInputEvent> events);
    void DiscardTextInputEvents() noexcept;
    void ResetTextInput() noexcept;
    void SetImeCaretAnchor(ImeCaretAnchor anchor) noexcept;
    ImeCaretAnchor GetImeCaretAnchor() const noexcept { return imeCaretAnchor_; }
    void Update(float unscaledDeltaSeconds);

    const InputSnapshot& GetSnapshot() const noexcept { return snapshot_; }
    const InputActionMap& GetActionMap() const noexcept { return actionMap_; }
    InputActionMap& EditActionMap() noexcept { return actionMap_; }
    const std::filesystem::path& GetInputDirectory() const noexcept { return inputDirectory_; }
    InputContextStack& Contexts() noexcept { return contexts_; }
    const InputContextStack& Contexts() const noexcept { return contexts_; }
    InputUser& GetPrimaryUser() noexcept { return primaryUser_; }
    const InputUser& GetPrimaryUser() const noexcept { return primaryUser_; }

    bool SaveProject(std::string* errorMessage = nullptr);
    bool ReloadProject(std::string* errorMessage = nullptr);
    bool RestoreDefaults(std::string* errorMessage = nullptr);
    bool BeginRebind(const InputRebindOptions& options = {});
    void CancelRebind();
    void ResetRebind();
    bool ConsumeRebindResult(InputRebindResult& outResult);
    const InputRebindOperation& GetRebindOperation() const noexcept { return rebindOperation_; }
    bool SetGamepadVibration(float lowFrequency, float highFrequency, float durationSeconds = 0.0f);
    void StopGamepadVibration();
    bool IsGamepadVibrating() const noexcept { return vibrationActive_; }
    const std::string& GetLastError() const noexcept { return lastError_; }

private:
    float ReadBindingValue(const InputBinding& binding) const;
    bool IsModifierDown(const std::string& modifier) const;
    uint32_t ResolveGamepadIndex() const;
    void UpdateLastActiveDevice(const InputDeviceState& previousDevices, float unscaledDeltaSeconds);
    void ResetRuntimeState();
    void EvaluateActions(float dt);
    void SuppressActions();

    std::unique_ptr<IInputBackend> backend_{};
    void* hostWindow_ = nullptr;
    std::filesystem::path inputDirectory_{};
    InputActionMap actionMap_{};
    InputContextStack contexts_{};
    InputUser primaryUser_{};
    InputSnapshot snapshot_{};
    InputRebindOperation rebindOperation_{};
    InputDeviceState rebindPreviousDevices_{};
    std::vector<TextInputEvent> externalTextInputEvents_{};
    ImeCaretAnchor imeCaretAnchor_{};
    float rebindReleaseThreshold_ = 0.25f;
    bool suppressActionsUntilNeutral_ = false;
    bool uiInteractionActive_ = false;
    bool developerInputCaptured_ = false;
    bool pointerCursorHiddenRequested_ = false;
    std::string lastError_{};
    float vibrationRemaining_ = 0.0f;
    uint32_t vibrationGamepadIndex_ = 0u;
    bool vibrationActive_ = false;
    float deviceSwitchLockRemainingSeconds_ = 0.0f;
};

} // namespace HIKARI::INPUT
