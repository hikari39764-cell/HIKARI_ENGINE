#include "Runtime/HIKARI_GamePresentationController.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_Dx12Core.h"

namespace HIKARI::RUNTIME {

    bool GamePresentationController::Begin(
        GFX::Dx12Core& core,
        PLATFORM::Win32Window& editorWindow,
        const GamePresentationConfig& config,
        ResizeCallback onResize) {
        if (state_ != State::Inactive || editorWindow.GetHWND() == nullptr) {
            return false;
        }

        const int gameWidth = (std::max)(config.width, 16);
        const int gameHeight = (std::max)(config.height, 16);
        if (!gameWindow_.Initialize(
                config.title,
                gameWidth,
                gameHeight,
                config.resizable,
                PLATFORM::WindowCloseBehavior::SignalOnly,
                false)) {
            HIKARI_LOG_ERROR("In-process game window creation failed.");
            return false;
        }
        gameWindow_.SetResizeCallback(std::move(onResize));
        if (!gameWindow_.ApplyWindowMode(config.windowMode, gameWidth, gameHeight)) {
            HIKARI_LOG_ERROR("In-process game window presentation mode failed.");
            gameWindow_.Shutdown();
            return false;
        }
        if (!core.CreateGamePresentationSurface(
                gameWindow_.GetHWND(),
                gameWindow_.Width(),
                gameWindow_.Height())) {
            HIKARI_LOG_ERROR("In-process game swap-chain handoff failed.");
            gameWindow_.Shutdown();
            return false;
        }

        state_ = State::Active;
        ShowWindow(editorWindow.GetHWND(), SW_HIDE);
        SetForegroundWindow(gameWindow_.GetHWND());
        HIKARI_LOG_INFO("In-process game presentation target activated.");
        return true;
    }

    void GamePresentationController::MarkStopping() {
        if (state_ != State::Active) {
            return;
        }
        (void)gameWindow_.SetTitle(L"HIKARI Game Preview | Stopping...");
    }

    bool GamePresentationController::ReleaseGameSurface(GFX::Dx12Core& core) {
        if (state_ == State::SurfaceReleased) {
            return true;
        }
        if (state_ != State::Active) {
            return false;
        }
        if (!core.HasGamePresentationSurface()) {
            state_ = State::SurfaceReleased;
            return true;
        }
        if (!core.ReleaseGamePresentationSurface()) {
            HIKARI_LOG_ERROR("Game presentation surface release failed.");
            return false;
        }
        state_ = State::SurfaceReleased;
        return true;
    }

    bool GamePresentationController::RestoreEditorSurface(
        GFX::Dx12Core& core,
        PLATFORM::Win32Window& editorWindow) {
        if (state_ == State::Inactive) {
            return true;
        }
        if (state_ != State::SurfaceReleased ||
            editorWindow.GetHWND() == nullptr ||
            !core.ActivateEditorPresentationSurface()) {
            HIKARI_LOG_ERROR("Editor presentation surface restoration failed.");
            return false;
        }

        gameWindow_.Shutdown();
        // Showing the hidden editor at its current placement preserves the
        // maximized or windowed state it had before Play.
        ShowWindow(editorWindow.GetHWND(), SW_SHOW);
        SetForegroundWindow(editorWindow.GetHWND());
        if (!editorWindow.RefreshClientSize()) {
            HIKARI_LOG_ERROR(
                "Editor window client size could not be refreshed after Play.");
            return false;
        }

        const GFX::Context editorContext = core.BuildContext();
        const uint32_t editorWidth =
            static_cast<uint32_t>(editorWindow.Width());
        const uint32_t editorHeight =
            static_cast<uint32_t>(editorWindow.Height());
        if ((editorContext.backBufferWidth != editorWidth ||
             editorContext.backBufferHeight != editorHeight) &&
            !core.Resize(editorWindow.Width(), editorWindow.Height())) {
            HIKARI_LOG_ERROR(
                "Editor presentation surface could not be synchronized to the restored window size.");
            return false;
        }

        state_ = State::Inactive;
        HIKARI_LOG_INFO(
            std::string("Editor presentation surface restored. size=") +
            std::to_string(editorWindow.Width()) + "x" +
            std::to_string(editorWindow.Height()) + ".");
        return true;
    }

    void GamePresentationController::Shutdown() {
        state_ = State::Inactive;
        gameWindow_.Shutdown();
    }

    bool GamePresentationController::HasCloseRequest() const {
        return state_ == State::Active && gameWindow_.HasCloseRequest();
    }

    PLATFORM::Win32Window* GamePresentationController::GetGameWindow() {
        return state_ != State::Inactive ? &gameWindow_ : nullptr;
    }

    const PLATFORM::Win32Window* GamePresentationController::GetGameWindow() const {
        return state_ != State::Inactive ? &gameWindow_ : nullptr;
    }

} // namespace HIKARI::RUNTIME
