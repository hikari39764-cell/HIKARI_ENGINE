#pragma once

#include <functional>

namespace HIKARI { class DocumentSceneBase; }

namespace HIKARI::EDITOR {
    class DialogueWorkspaceController;
    class EditorOperationRegistry;

    void RegisterDialogueOperations(
        EditorOperationRegistry& registry,
        DialogueWorkspaceController& controller,
        std::function<DocumentSceneBase*()> sceneProvider);

} // namespace HIKARI::EDITOR
