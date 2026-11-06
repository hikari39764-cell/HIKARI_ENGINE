#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "Input/Assets/HIKARI_InputActionMap.h"
#include "Input/Assets/HIKARI_InputTrack.h"
#include "Input/Runtime/HIKARI_InputContextStack.h"
#include "Input/Runtime/HIKARI_InputRebindOperation.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
#include "Input/Runtime/HIKARI_InputUser.h"

namespace HIKARI::INPUT {

class IInputBackend;

// 開発者の道具がキーボードとマウスを持つ理由。どれかが持っている間は Global 以外の動作を評価しない（生の入力は読める）。
enum class DeveloperInputOwner : uint8_t {
    // 開発コンソールが開いている（打鍵は入力欄のもの）。
    Console = 1u << 0u,
    // 自由視点がゲームの相機を借りている（キーとマウスは飛行に使う）。
    FreeCamera = 1u << 1u,
};

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
    bool IsUiInteractionActive() const noexcept { return uiInteractionActive_ || IsDeveloperInputCaptured(); }
    void SetDeveloperInputCaptured(DeveloperInputOwner owner, bool active) noexcept;
    void ReleaseDeveloperInput() noexcept;
    bool IsDeveloperInputCaptured() const noexcept { return developerInputOwners_ != 0u; }
    bool IsDeveloperInputCapturedBy(DeveloperInputOwner owner) const noexcept {
        return (developerInputOwners_ & static_cast<uint8_t>(owner)) != 0u;
    }
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

    // 入力轨道の回放。tick は SetSimulationTick で受け取る固定歩長の完了数で数え、次の Update を 0 とする。
    // 轨道の値は割り当てと同じ規則で動作に加わる（ボタンは最大値、軸は和）。轨道が地図に無い動作を使えば false。
    bool BeginPlayback(InputTrack track, std::string& outError);
    void StopPlayback() noexcept;
    struct PlaybackStatus {
        bool active = false;
        uint64_t tick = 0;
        uint64_t length = 0;
    };
    PlaybackStatus GetPlaybackStatus() const noexcept;
    // フレームの最初に、これから進める固定歩長の完了数を渡す（回放の時刻）。
    void SetSimulationTick(uint64_t tick) noexcept { simulationTick_ = tick; }

private:
    float ReadBindingValue(const InputBinding& binding) const;
    bool IsModifierDown(const std::string& modifier) const;
    uint32_t ResolveGamepadIndex() const;
    void UpdateLastActiveDevice(const InputDeviceState& previousDevices, float unscaledDeltaSeconds);
    void ResetRuntimeState();
    void EvaluateActions(float dt);
    void ApplyPlayback();
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
    uint8_t developerInputOwners_ = 0u;
    bool pointerCursorHiddenRequested_ = false;
    std::string lastError_{};
    float vibrationRemaining_ = 0.0f;
    uint32_t vibrationGamepadIndex_ = 0u;
    bool vibrationActive_ = false;
    float deviceSwitchLockRemainingSeconds_ = 0.0f;
    std::optional<InputTrack> playback_{};
    std::optional<uint64_t> playbackStartTick_{};
    size_t playbackCursor_ = 0;
    std::unordered_map<std::string, std::array<float, 2>> playbackValues_{};
    uint64_t simulationTick_ = 0;
};

} // namespace HIKARI::INPUT
