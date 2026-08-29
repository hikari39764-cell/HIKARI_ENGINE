#pragma once

#include <string>

namespace HIKARI::EDITOR {

    enum class EditorProjectTransition {
        None,
        ReturnToProjectHub,
        RebuildAndReload,
    };

    // Project process transitions are intentionally human-only. They close the
    // current editor process and therefore require an in-context confirmation
    // instead of being exposed through the EditorOperation automation bus.
    class EditorProjectSessionController {
    public:
        void DrawMainMenu(
            bool transitionAllowed,
            bool hasUnsavedChanges);
        void DrawConfirmationDialog();

        bool ShouldExitHost() const noexcept;

    private:
        void RequestTransition(
            EditorProjectTransition transition,
            bool hasUnsavedChanges);
        bool LaunchProjectHub(
            bool rebuildAndReload,
            std::string& outMessage);

        EditorProjectTransition pendingTransition_ =
            EditorProjectTransition::None;
        std::string statusMessage_{};
        bool hasUnsavedChanges_ = false;
        bool openConfirmationRequested_ = false;
        bool exitRequested_ = false;
    };

} // namespace HIKARI::EDITOR
