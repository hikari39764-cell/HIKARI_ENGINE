#pragma once

#include <functional>

#include "Platform/HIKARI_Win32Window.h"

namespace HIKARI::GFX {
    class Dx12Core;
}

namespace HIKARI::RUNTIME {

    struct GamePresentationConfig {
        const wchar_t* title = L"HIKARI Game";
        int width = 1280;
        int height = 720;
        bool resizable = true;
        PLATFORM::WindowMode windowMode = PLATFORM::WindowMode::Windowed;
    };

    class GamePresentationController {
    public:
        using ResizeCallback = std::function<void(int, int)>;

        bool Begin(
            GFX::Dx12Core& core,
            PLATFORM::Win32Window& editorWindow,
            const GamePresentationConfig& config,
            ResizeCallback onResize);
        void MarkStopping();
        bool ReleaseGameSurface(GFX::Dx12Core& core);
        bool RestoreEditorSurface(
            GFX::Dx12Core& core,
            PLATFORM::Win32Window& editorWindow);
        void Shutdown();

        bool IsActive() const { return state_ != State::Inactive; }
        bool IsGameSurfaceActive() const { return state_ == State::Active; }
        bool IsGameSurfaceReleased() const {
            return state_ == State::SurfaceReleased;
        }
        bool HasCloseRequest() const;
        PLATFORM::Win32Window* GetGameWindow();
        const PLATFORM::Win32Window* GetGameWindow() const;

    private:
        enum class State {
            Inactive,
            Active,
            SurfaceReleased,
        };

        PLATFORM::Win32Window gameWindow_{};
        State state_ = State::Inactive;
    };

} // namespace HIKARI::RUNTIME
