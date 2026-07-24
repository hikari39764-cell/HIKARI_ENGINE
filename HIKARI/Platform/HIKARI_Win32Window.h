#pragma once

#include <windows.h>
#include <filesystem>
#include <functional>
#include <vector>

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

class Win32Window {
public:
    using ResizeCallback = std::function<void(int, int)>;

    bool Initialize(
        const wchar_t* title,
        int width,
        int height,
        bool resizable = true,
        WindowCloseBehavior closeBehavior = WindowCloseBehavior::QuitApplication,
        bool dispatchImGuiInput = true);
    void Shutdown();

    bool PumpMessages();
    float ConsumeMouseWheelDelta();
    bool SetTitle(const wchar_t* title);
    // Refreshes the cached client dimensions without dispatching the resize
    // callback. Presentation handoffs use this before synchronizing a surface.
    bool RefreshClientSize();

    HWND GetHWND() const { return hwnd_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool IsRunning() const { return running_; }
    bool HasCloseRequest() const { return closeRequested_; }
    bool IsMinimized() const { return isMinimized_; }
    WindowMode GetWindowMode() const { return windowMode_; }

    void SetResizeCallback(ResizeCallback cb) { onResize_ = std::move(cb); }
    bool ApplyWindowMode(WindowMode mode, int clientWidth, int clientHeight);

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
    HWND hwnd_{};
    HINSTANCE hInstance_{};
    int width_{};
    int height_{};
    bool resizable_ = true;
    bool isMinimized_{};
    bool running_{};
    bool closeRequested_{};
    WindowCloseBehavior closeBehavior_ = WindowCloseBehavior::QuitApplication;
    bool dispatchImGuiInput_ = true;
    float mouseWheelDelta_{};
    ResizeCallback onResize_{};
    WindowMode windowMode_ = WindowMode::Windowed;
    DWORD windowedStyle_ = WS_OVERLAPPEDWINDOW;
    WINDOWPLACEMENT windowedPlacement_{ sizeof(WINDOWPLACEMENT) };
};

std::vector<std::filesystem::path> ConsumeDroppedFiles();

} // namespace HIKARI::PLATFORM
