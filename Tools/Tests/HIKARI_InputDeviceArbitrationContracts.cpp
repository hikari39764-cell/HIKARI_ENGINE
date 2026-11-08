#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Core/HIKARI_TimeService.h"
#include "Input/Assets/HIKARI_InputTrack.h"
#include "Input/Platform/HIKARI_IInputBackend.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "HIKARI/Project/FrameContext.h"

namespace {

class FakeInputBackend final : public HIKARI::INPUT::IInputBackend {
public:
    void SetFrame(HIKARI::INPUT::InputDeviceState frame) { frame_ = std::move(frame); }

    void SetHostWindow(void*) override {}
    bool HasInputFocus() const noexcept override { return focused; }
    bool focused = true;
    float lowMotor = 0, highMotor = 0;
    uint32_t motorIndex = 0;
    void SetMouseCaptureMode(HIKARI::INPUT::MouseCaptureMode mode) override { mouseCaptureMode_ = mode; }
    void SetMouseCaptureRegion(const HIKARI::INPUT::MouseCaptureRegion&) override { hasCaptureRegion = true; }
    void ClearMouseCaptureRegion() override { hasCaptureRegion = false; }
    bool hasCaptureRegion = false;
    HIKARI::INPUT::MouseCaptureMode GetMouseCaptureMode() const noexcept override { return mouseCaptureMode_; }
    void SetExternalMouseWheel(float) override {}
    void ResetTextInput() noexcept override { ++textInputResetCount; }
    void SetImeCaretAnchor(HIKARI::INPUT::ImeCaretAnchor anchor) noexcept override { imeCaretAnchor = anchor; }
    void SetPointerCursorVisible(bool visible) override { cursorVisible = visible; }
    bool cursorVisible = true;
    uint32_t textInputResetCount = 0u;
    HIKARI::INPUT::ImeCaretAnchor imeCaretAnchor{};
    void Reset() override {
        cursorVisible = true;
        frame_ = {};
    }
    void Poll(HIKARI::INPUT::InputDeviceState& out) override { out = frame_; }
    float ReadControl(const HIKARI::INPUT::InputDeviceState& state, HIKARI::INPUT::InputBindingSource source,
                      std::string_view control, uint32_t) const override {
        if (source == HIKARI::INPUT::InputBindingSource::Keyboard) {
            if (control.size() == 1)
                return state.keyboard[static_cast<unsigned char>(control[0])] ? 1.0f : 0.0f;
            if (control == "Enter")
                return state.keyboard[13] ? 1.0f : 0.0f;
        }
        return 0.0f;
    }
    bool IsAnyControlActive(const HIKARI::INPUT::InputDeviceState&, float) const override { return false; }
    void EnumerateNewControls(const HIKARI::INPUT::InputDeviceState&, const HIKARI::INPUT::InputDeviceState&, float,
                              bool, std::vector<HIKARI::INPUT::InputControlActuation>& out) const override {
        out.clear();
    }
    bool SetGamepadVibration(uint32_t index, float low, float high) override {
        motorIndex = index;
        lowMotor = low;
        highMotor = high;
        return true;
    }

private:
    HIKARI::INPUT::InputDeviceState frame_{};
    HIKARI::INPUT::MouseCaptureMode mouseCaptureMode_ = HIKARI::INPUT::MouseCaptureMode::Free;
};

bool ExpectDevice(const HIKARI::INPUT::InputService& service, HIKARI::INPUT::InputDeviceKind expected,
                  const char* message) {
    if (service.GetSnapshot().GetLastActiveDevice() == expected) {
        return true;
    }
    std::cerr << message << '\n';
    return false;
}

bool CheckTextInputFrameLifetime(HIKARI::INPUT::InputService& service, FakeInputBackend& backend) {
    using namespace HIKARI::INPUT;
    std::vector<TextInputEvent> events{};
    events.push_back({TextInputEventKind::CompositionUpdated, "に"});
    events.push_back({TextInputEventKind::Committed, "日"});
    service.SetExternalTextInputEvents(std::move(events));
    service.Update(1.0f / 60.0f);
    const auto& received = service.GetSnapshot().GetTextInputEvents();
    if (received.size() != 2u || received[0].kind != TextInputEventKind::CompositionUpdated ||
        received[0].utf8 != "に" || received[1].kind != TextInputEventKind::Committed || received[1].utf8 != "日") {
        std::cerr << "text input events were not transferred into the frame snapshot\n";
        return false;
    }

    service.Update(1.0f / 60.0f);
    if (!service.GetSnapshot().GetTextInputEvents().empty()) {
        std::cerr << "text input events survived into a second frame\n";
        return false;
    }

    service.SetExternalTextInputEvents({{TextInputEventKind::Committed, "stale"}});
    service.Update(1.0f / 60.0f);
    service.DiscardTextInputEvents();
    if (!service.GetSnapshot().GetTextInputEvents().empty()) {
        std::cerr << "discard did not clear events already published in the current frame\n";
        return false;
    }

    service.SetExternalTextInputEvents({{TextInputEventKind::CompositionUpdated, "old field"}});
    service.Update(1.0f / 60.0f);
    const uint32_t resetCountBeforeCapture = backend.textInputResetCount;
    service.SetDeveloperInputCaptured(DeveloperInputOwner::Console, true);
    if (!service.GetSnapshot().GetTextInputEvents().empty() || backend.textInputResetCount <= resetCountBeforeCapture) {
        std::cerr << "developer input capture did not reset text composition state\n";
        return false;
    }
    // コンソールと自由視点は別々に持つ。片方を離しても、もう片方が持っている間は捕まえたまま。
    service.SetDeveloperInputCaptured(DeveloperInputOwner::FreeCamera, true);
    service.SetDeveloperInputCaptured(DeveloperInputOwner::Console, false);
    if (!service.IsDeveloperInputCaptured() || service.IsDeveloperInputCapturedBy(DeveloperInputOwner::Console) ||
        !service.IsDeveloperInputCapturedBy(DeveloperInputOwner::FreeCamera)) {
        std::cerr << "releasing the console dropped the free camera's developer input capture\n";
        return false;
    }
    service.SetDeveloperInputCaptured(DeveloperInputOwner::Console, true);
    service.ReleaseDeveloperInput();
    if (service.IsDeveloperInputCaptured()) {
        std::cerr << "releasing developer input kept an owner\n";
        return false;
    }

    const ImeCaretAnchor anchor{17, 29, true};
    service.SetImeCaretAnchor(anchor);
    if (!backend.imeCaretAnchor.valid || backend.imeCaretAnchor.clientX != anchor.clientX ||
        backend.imeCaretAnchor.clientY != anchor.clientY) {
        std::cerr << "IME caret anchor was not forwarded to the input backend\n";
        return false;
    }
    service.ResetTextInput();
    if (service.GetImeCaretAnchor().valid || backend.imeCaretAnchor.valid) {
        std::cerr << "text input reset retained the old IME caret anchor\n";
        return false;
    }
    return true;
}

// 入力轨道：严格読み取りと、固定歩長の tick に沿った回放（値は次の変化まで保持、length で終わる）。
// 时间服务の锁步：一時停止中でも指定フレーム数だけ固定歩長ちょうどずつ進む。
bool CheckInputTrackPlaybackAndLockstep(HIKARI::INPUT::InputService& service, FakeInputBackend& fake) {
    using namespace HIKARI::INPUT;
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            std::cerr << message << '\n';
            passed = false;
        }
    };
    InputTrack track;
    std::string error;
    const auto read = [&](const char* text) { return ReadInputTrack(nlohmann::json::parse(text), track, error); };
    check(!read(R"({"assetType":"InputTrack","formatVersion":2,"length":4,"events":[]})"),
          "input track accepted an unsupported format version");
    check(
        !read(
            R"({"assetType":"InputTrack","formatVersion":1,"length":4,"events":[{"tick":4,"action":"Gameplay.Jump","value":1}]})"),
        "input track accepted an event past its length");
    check(
        !read(
            R"({"assetType":"InputTrack","formatVersion":1,"length":4,"events":[{"tick":0,"action":"Gameplay.Move","value":[1]}]})"),
        "input track accepted a malformed value");
    check(
        read(
            R"({"assetType":"InputTrack","formatVersion":1,"length":4,"events":[{"tick":0,"action":"Missing.Action","value":1}]})") &&
            !service.BeginPlayback(track, error),
        "playback accepted an action that is not in the action map");
    check(read(R"({"assetType":"InputTrack","formatVersion":1,"length":5,"events":[
        {"tick":2,"action":"Gameplay.Move","value":[0,0]},{"tick":0,"action":"Gameplay.Move","value":[0,1]},
        {"tick":1,"action":"Gameplay.Jump","value":1},{"tick":3,"action":"Gameplay.Jump","value":0}]})"),
          "valid input track was rejected");
    check(service.BeginPlayback(track, error), "valid input track did not start");
    fake.SetFrame({});
    const auto frameAt = [&](uint64_t tick) {
        service.SetSimulationTick(tick);
        service.Update(1.0f / 60.0f);
        return service.GetSnapshot();
    };
    auto snapshot = frameAt(100);
    check(snapshot.GetAxis2D("Gameplay.Move")[1] == 1.0f && !snapshot.IsDown("Gameplay.Jump"),
          "track tick 0 did not set Move");
    snapshot = frameAt(101);
    check(snapshot.IsPressed("Gameplay.Jump") && snapshot.GetAxis2D("Gameplay.Move")[1] == 1.0f,
          "track tick 1 did not press Jump while keeping Move");
    snapshot = frameAt(102);
    check(snapshot.IsDown("Gameplay.Jump") && !snapshot.IsPressed("Gameplay.Jump") &&
              snapshot.GetAxis2D("Gameplay.Move")[1] == 0.0f,
          "track tick 2 did not release Move while holding Jump");
    snapshot = frameAt(103);
    check(!snapshot.IsDown("Gameplay.Jump") && service.GetPlaybackStatus().active &&
              service.GetPlaybackStatus().tick == 3u,
          "track tick 3 did not release Jump");
    frameAt(105);
    check(!service.GetPlaybackStatus().active, "input track did not end at its length");

    HIKARI::TIME::Reset();
    HIKARI::TIME::SetPaused(true);
    check(HIKARI::TIME::BeginFrame().gameDt == 0.0f, "paused time advanced");
    HIKARI::TIME::SetLockstepFrames(2u);
    for (int frame = 0; frame < 2; ++frame) {
        const HIKARI::FrameContext& context = HIKARI::TIME::BeginFrame();
        check(context.gameDt == context.fixedDt && !HIKARI::TIME::IsPaused(),
              "lockstep frame did not advance one tick");
    }
    check(HIKARI::TIME::BeginFrame().gameDt == 0.0f && HIKARI::TIME::IsPaused() &&
              HIKARI::TIME::GetLockstepFramesRemaining() == 0u,
          "time did not return to pause after lockstep");
    HIKARI::TIME::Reset();
    return passed;
}

bool CheckVibrationLifecycle(HIKARI::INPUT::InputService& service, FakeInputBackend& fake) {
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            std::cerr << message << '\n';
            passed = false;
        }
    };
    check(service.SetGamepadVibration(0.3f, 0.7f, 0), "focused vibration was rejected");
    service.Update(0.5f);
    check(service.IsGamepadVibrating() && fake.highMotor == 0.7f, "continuous pulse ended early");
    fake.focused = false;
    service.Update(0.01f);
    check(!service.IsGamepadVibrating() && fake.lowMotor == 0 && fake.highMotor == 0,
          "focus loss did not stop both motors");
    check(!service.SetGamepadVibration(1, 1, 0.1f), "unfocused vibration was accepted");
    fake.focused = true;
    service.Update(0.01f);
    check(!service.IsGamepadVibrating() && fake.highMotor == 0, "focus regain replayed an old pulse");
    service.GetPrimaryUser().AssignGamepad(2);
    check(service.SetGamepadVibration(0.2f, 0.6f, 0.1f), "focused timed pulse was rejected");
    service.GetPrimaryUser().AssignGamepad(3);
    fake.focused = false; // after polling, before the next submission
    check(!service.SetGamepadVibration(1, 1, 0.1f) && !service.IsGamepadVibrating() && fake.motorIndex == 2 &&
              fake.highMotor == 0,
          "submission lost focus or stopped the wrong controller");
    check(service.SetGamepadVibration(0, 0, 0), "explicit zero was rejected while unfocused");
    fake.focused = true;
    check(service.SetGamepadVibration(0.3f, 0.4f, 0.1f), "timed pulse was rejected");
    service.Update(0.05f);
    check(service.IsGamepadVibrating(), "timed pulse stopped early");
    service.Update(0.06f);
    check(!service.IsGamepadVibrating() && fake.highMotor == 0, "timed pulse failed to stop");
    check(service.SetGamepadVibration(1, 1, 0), "host-switch fixture failed");
    int host = 0;
    service.SetHostWindow(&host);
    check(!service.IsGamepadVibrating() && fake.highMotor == 0, "host switch retained vibration");
    service.SetHostWindow(nullptr);
    service.GetPrimaryUser().AssignGamepad(0);
    return passed;
}

bool CheckPointerRelease(HIKARI::INPUT::InputService& service, FakeInputBackend& fake) {
    using namespace HIKARI;
    using INPUT::DeveloperInputOwner;
    using INPUT::MouseCaptureMode;
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) {
            std::cerr << message << '\n';
            passed = false;
        }
    };
    service.SetMouseCaptureRegion({10, 20, 810, 620});
    service.SetMouseCaptureMode(MouseCaptureMode::Relative);
    service.ApplyPointerCursorVisibility(true);
    check(service.GetMouseCaptureMode() == MouseCaptureMode::Relative && fake.hasCaptureRegion,
          "owned gameplay lost relative capture");
    service.ApplyPointerCursorVisibility(false);
    check(service.GetMouseCaptureMode() == MouseCaptureMode::Free && !fake.hasCaptureRegion && fake.cursorVisible,
          "returning pointer to editor left relative capture active");

    return passed;
}

} // namespace

int main() {
    auto backend = std::make_unique<FakeInputBackend>();
    FakeInputBackend* fake = backend.get();
    HIKARI::INPUT::InputService service{};
    service.SetBackend(std::move(backend));
    if (!service.InitializeTransient()) {
        std::cerr << "unable to initialize transient input service\n";
        return 1;
    }
    if (!CheckTextInputFrameLifetime(service, *fake)) {
        return 29;
    }
    if (!CheckInputTrackPlaybackAndLockstep(service, *fake)) {
        return 31;
    }

    constexpr float kFrameSeconds = 1.0f / 60.0f;
    const auto tick = [&](const HIKARI::INPUT::InputDeviceState& frame) {
        fake->SetFrame(frame);
        service.Update(kFrameSeconds);
    };

    HIKARI::INPUT::InputDeviceState frame{};
    frame.keyboard[static_cast<std::size_t>('W')] = 1u;
    tick(frame);
    if (!ExpectDevice(service, HIKARI::INPUT::InputDeviceKind::KeyboardMouse,
                      "keyboard press did not select keyboard/mouse")) {
        return 2;
    }

    frame.gamepads[0].connected = true;
    frame.gamepads[0].leftX = 0.28f;
    tick(frame);
    frame.gamepads[0].leftX = 0.36f;
    tick(frame);
    frame.gamepads[0].leftX = 0.34f;
    tick(frame);
    frame.gamepads[0].leftX = 0.36f;
    tick(frame);
    if (!ExpectDevice(service, HIKARI::INPUT::InputDeviceKind::KeyboardMouse,
                      "near-dead-zone stick drift stole the active device")) {
        return 3;
    }

    frame.keyboard = {};
    frame.gamepads[0].leftX = 0.72f;
    tick(frame);
    if (!ExpectDevice(service, HIKARI::INPUT::InputDeviceKind::Gamepad,
                      "intentional stick motion did not select gamepad")) {
        return 4;
    }

    frame.mouseDeltaX = 5.0f;
    tick(frame);
    if (!ExpectDevice(service, HIKARI::INPUT::InputDeviceKind::KeyboardMouse,
                      "intentional mouse motion did not select keyboard/mouse")) {
        return 5;
    }

    frame.mouseDeltaX = 0.0f;
    tick(frame);
    if (!ExpectDevice(service, HIKARI::INPUT::InputDeviceKind::KeyboardMouse,
                      "unchanged held stick stole the active device back")) {
        return 6;
    }

    frame.gamepads[0].leftX = 0.86f;
    tick(frame);
    if (!ExpectDevice(service, HIKARI::INPUT::InputDeviceKind::Gamepad,
                      "meaningful stick change did not restore gamepad")) {
        return 7;
    }

    frame.keyboard[static_cast<std::size_t>('A')] = 1u;
    tick(frame);
    if (!ExpectDevice(service, HIKARI::INPUT::InputDeviceKind::KeyboardMouse,
                      "fresh keyboard input did not override a held stick")) {
        return 8;
    }

    frame.gamepads[1].connected = true;
    frame.gamepads[1].leftX = 0.95f;
    tick(frame);
    if (!ExpectDevice(service, HIKARI::INPUT::InputDeviceKind::KeyboardMouse,
                      "unassigned gamepad stole the primary user's device")) {
        return 9;
    }

    frame.keyboard = {};
    frame.gamepads[0].buttons = 0u;
    tick(frame);
    frame.keyboard[static_cast<std::size_t>('D')] = 1u;
    frame.gamepads[0].buttons = 1u;
    tick(frame);
    if (!ExpectDevice(service, HIKARI::INPUT::InputDeviceKind::KeyboardMouse,
                      "simultaneous digital reports should retain the prior device")) {
        return 10;
    }

    frame.keyboard = {};
    frame.gamepads[0].buttons = 0u;
    tick(frame);
    frame.gamepads[0].buttons = 2u;
    tick(frame);
    if (!ExpectDevice(service, HIKARI::INPUT::InputDeviceKind::Gamepad,
                      "fresh gamepad button did not select gamepad")) {
        return 11;
    }

    frame.mouseDeltaX = 0.5f;
    tick(frame);
    if (!ExpectDevice(service, HIKARI::INPUT::InputDeviceKind::Gamepad,
                      "sub-threshold mouse jitter stole the active device")) {
        return 12;
    }

    frame.gamepads[0] = {};
    frame.mouseDeltaX = 0.0f;
    tick(frame);
    if (!ExpectDevice(service, HIKARI::INPUT::InputDeviceKind::KeyboardMouse,
                      "primary gamepad disconnect did not restore keyboard/mouse")) {
        return 13;
    }

    service.Update(kFrameSeconds);
    service.SetPointerCursorVisible(false);
    service.ApplyPointerCursorVisibility(true);
    if (fake->cursorVisible)
        return 14;
    service.ApplyPointerCursorVisibility(false);
    if (!fake->cursorVisible)
        return 15;
    service.ApplyPointerCursorVisibility(true);
    if (fake->cursorVisible)
        return 16;
    service.Update(kFrameSeconds);
    service.ApplyPointerCursorVisibility(true);
    if (!fake->cursorVisible)
        return 17;
    service.SetPointerCursorVisible(false);
    service.SetMouseCaptureMode(HIKARI::INPUT::MouseCaptureMode::Relative);
    service.ApplyPointerCursorVisibility(true);
    if (!fake->cursorVisible)
        return 18;
    service.SetMouseCaptureMode(HIKARI::INPUT::MouseCaptureMode::Free);
    service.SetPointerCursorVisible(true);
    service.ApplyPointerCursorVisibility(true);
    if (!fake->cursorVisible)
        return 19;
    service.SetPointerCursorVisible(false);
    service.ApplyPointerCursorVisibility(true);
    service.InitializeTransient();
    service.ApplyPointerCursorVisibility(true);
    if (!fake->cursorVisible)
        return 20;
    service.SetPointerCursorVisible(false);
    service.ApplyPointerCursorVisibility(true);
    service.InitializeTransient();
    service.Contexts().SetActive("Gameplay", true);
    frame = {};
    frame.keyboard['W'] = 1;
    frame.keyboard['E'] = 1;
    tick(frame);
    if (service.GetSnapshot().FindAction("Gameplay.Move")->y < 0.9f)
        return 22;
    service.Contexts().SetActive("UI", true);
    tick(frame);
    if (service.GetSnapshot().FindAction("UI.Navigate")->y < 0.9f ||
        !service.GetSnapshot().FindAction("UI.Submit")->down ||
        service.GetSnapshot().FindAction("Gameplay.Move")->y != 0 ||
        service.GetSnapshot().FindAction("Gameplay.Interact")->down)
        return 23;
    service.Contexts().SetActive("UI", false);
    tick(frame);
    if (service.GetSnapshot().FindAction("Gameplay.Move")->y < 0.9f ||
        service.GetSnapshot().FindAction("UI.Navigate")->y != 0)
        return 24;
    if (!CheckPointerRelease(service, *fake))
        return 28;
    if (!CheckVibrationLifecycle(service, *fake))
        return 29;
    if (!service.SetGamepadVibration(1, 1, 0))
        return 30;
    service.Shutdown();
    if (!fake->cursorVisible || fake->highMotor != 0 || service.IsGamepadVibrating())
        return 21;

    std::cout << "Input device and pointer visibility contracts passed.\n";
    return 0;
}
