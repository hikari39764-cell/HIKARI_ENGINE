#pragma once

#include <windows.h>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Platform/HIKARI_TextInputTypes.h"

namespace HIKARI::PLATFORM {

enum class WindowCloseBehavior {
    QuitApplication,
    SignalOnly,
};

enum class WindowMode {
    Windowed = 0,
    BorderlessWindow,
    Fullscreen,
};

// 今のペンの筆圧（0..1）。ペンが触れていない時やマウスは 1。
float CurrentPenPressure() noexcept;

class Win32Window {
public:
    using ResizeCallback = std::function<void(int, int)>;
    using MessageHandler = std::function<bool(HWND, UINT, WPARAM, LPARAM)>;

    bool Initialize(const wchar_t* title, int width, int height, bool resizable = true,
                    WindowCloseBehavior closeBehavior = WindowCloseBehavior::QuitApplication,
                    bool dispatchHostMessages = true, bool startHidden = false);
    void Shutdown();

    bool PumpMessages();
    float ConsumeMouseWheelDelta();
    std::vector<INPUT::TextInputEvent> ConsumeTextInputEvents();
    void DiscardTextInputEvents() noexcept;
    static void ResetTextInputForWindow(HWND hwnd) noexcept;
    static void ApplyImeCaretAnchorForWindow(HWND hwnd, INPUT::ImeCaretAnchor anchor) noexcept;
    bool SetTitle(const wchar_t* title);
    void Show();
    bool IsStartupHidden() const { return startupHidden_; }
    // Refreshes the cached client dimensions without dispatching the resize
    // callback. Presentation handoffs use this before synchronizing a surface.
    bool RefreshClientSize();

    HWND GetHWND() const { return hwnd_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool IsRunning() const { return running_; }
    bool HasCloseRequest() const { return closeRequested_; }

    void SetResizeCallback(ResizeCallback cb) { onResize_ = std::move(cb); }
    void SetMessageHandler(MessageHandler handler) { messageHandler_ = std::move(handler); }
    bool ApplyWindowMode(WindowMode mode, int clientWidth, int clientHeight);

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void CaptureTextInput(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    void ResetTextInput(HWND hwnd, bool cancelComposition, bool publishCompositionEnded = false) noexcept;
    void AppendTextInput(INPUT::TextInputEventKind kind, std::string utf8 = {});
    void AppendUtf16Character(uint16_t value);
    void AppendUnicodeCharacter(uint32_t value);

private:
    HWND hwnd_{};
    bool startupHidden_ = false;
    HINSTANCE hInstance_{};
    int width_{};
    int height_{};
    bool resizable_ = true;
    bool isMinimized_{};
    bool running_{};
    bool closeRequested_{};
    WindowCloseBehavior closeBehavior_ = WindowCloseBehavior::QuitApplication;
    bool dispatchHostMessages_ = true;
    float mouseWheelDelta_{};
    std::vector<INPUT::TextInputEvent> textInputEvents_{};
    wchar_t pendingHighSurrogate_ = L'\0';
    bool imeCompositionActive_ = false;
    ResizeCallback onResize_{};
    MessageHandler messageHandler_{};
    WindowMode windowMode_ = WindowMode::Windowed;
    DWORD windowedStyle_ = WS_OVERLAPPEDWINDOW;
    WINDOWPLACEMENT windowedPlacement_{sizeof(WINDOWPLACEMENT)};
};

// Enables shell file drops for an auxiliary native editor window.
// The main HIKARI window registers itself during initialization.
void RegisterExternalFileDropWindow(HWND hwnd);
void UnregisterExternalFileDropWindow(HWND hwnd);

// Consumes only drops whose screen position is inside the authoring surface.
// This prevents one docked/floating panel from stealing another panel's drop.
std::vector<std::filesystem::path> ConsumeDroppedFilesInScreenRect(const RECT& screenRect);

} // namespace HIKARI::PLATFORM
