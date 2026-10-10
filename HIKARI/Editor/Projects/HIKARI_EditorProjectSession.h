#pragma once

#include <string>

namespace HIKARI::EDITOR {

enum class EditorProjectTransition {
    None,
    ReturnToProjectHub,
    RebuildAndReload,
};

// Project process transitions close the current editor process and therefore
// require an in-context confirmation: the menu shows its own dialog, and agents
// call project.reload, which the user approves in the agent panel every time.
class EditorProjectSessionController {
public:
    void DrawMainMenu(bool transitionAllowed, bool hasUnsavedChanges);
    void DrawConfirmationDialog();
    // 承認済みの「重新构建并重载」。呼ぶ側が未保存の文書と Play を確かめる。
    bool StartRebuildAndReload(std::string& outMessage);

    bool ShouldExitHost() const noexcept;

private:
    void RequestTransition(EditorProjectTransition transition, bool hasUnsavedChanges);
    bool LaunchProjectHub(bool rebuildAndReload, std::string& outMessage);

    EditorProjectTransition pendingTransition_ = EditorProjectTransition::None;
    std::string statusMessage_{};
    bool hasUnsavedChanges_ = false;
    bool openConfirmationRequested_ = false;
    bool exitRequested_ = false;
};

} // namespace HIKARI::EDITOR
