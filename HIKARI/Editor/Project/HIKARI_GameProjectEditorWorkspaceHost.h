#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "Editor/Project/HIKARI_GameProjectEditorWorkspaceRegistry.h"

namespace HIKARI::EDITOR {

    class GameProjectEditorModuleLibrary;
    class GameProjectEditorWorkspaceUi;

    class GameProjectEditorWorkspaceHost {
    public:
        GameProjectEditorWorkspaceHost();
        ~GameProjectEditorWorkspaceHost();

        GameProjectEditorWorkspaceHost(
            const GameProjectEditorWorkspaceHost&) = delete;
        GameProjectEditorWorkspaceHost& operator=(
            const GameProjectEditorWorkspaceHost&) = delete;

        bool RegisterModule(
            const GameProjectEditorModuleLibrary& module,
            std::string_view projectRootUtf8,
            std::string& outMessage);
        void ReportModuleFailure(std::string message);
        void Draw(float unscaledDeltaSeconds);
        void Shutdown() noexcept;

        bool IsAvailable() const noexcept;
        std::string_view GetModuleFailure() const noexcept;
        GameProjectEditorWorkspaceRegistry& Registry() noexcept;
        const GameProjectEditorWorkspaceRegistry& Registry() const noexcept;

    private:
        void DrawWorkspaceSelector();
        void DrawClosePrompt();

        GameProjectEditorWorkspaceRegistry registry_{};
        std::unique_ptr<GameProjectEditorWorkspaceUi> ui_{};
        std::string moduleFailureMessage_{};
        bool closePromptWasPending_ = false;
    };

} // namespace HIKARI::EDITOR
