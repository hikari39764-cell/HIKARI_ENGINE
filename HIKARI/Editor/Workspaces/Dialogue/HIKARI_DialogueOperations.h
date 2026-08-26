#pragma once

#include <functional>

namespace HIKARI { class DocumentSceneBase; }

namespace HIKARI::EDITOR {
    class DialogueWorkspaceController;
    class EditorOperationRegistry;
    class EditorWorkspaceHost;

    void RegisterDialogueOperations(
        EditorOperationRegistry& registry,
        DialogueWorkspaceController& controller,
        EditorWorkspaceHost& workspaceHost,
        std::function<DocumentSceneBase*()> sceneProvider);

} // namespace HIKARI::EDITOR
