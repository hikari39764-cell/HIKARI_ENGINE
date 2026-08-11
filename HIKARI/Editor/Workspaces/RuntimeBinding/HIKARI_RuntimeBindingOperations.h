#pragma once

#include <functional>

namespace HIKARI {
    class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

    class EditorOperationRegistry;
    class RuntimeBindingWorkspaceController;

    void RegisterRuntimeBindingOperations(
        EditorOperationRegistry& registry,
        RuntimeBindingWorkspaceController& controller,
        std::function<DocumentSceneBase*()> sceneProvider);

} // namespace HIKARI::EDITOR
