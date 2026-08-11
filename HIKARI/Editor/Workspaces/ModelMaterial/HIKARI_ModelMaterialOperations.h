#pragma once

#include <functional>

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

class EditorOperationRegistry;
class ModelMaterialWorkspaceController;

// Registers Model Material workspace operations on the unified editor
// operation bus. Human UI, AI actions and the local bridge all reach the
// workspace through these operations.
void RegisterModelMaterialOperations(
    EditorOperationRegistry& registry,
    ModelMaterialWorkspaceController& controller,
    std::function<DocumentSceneBase*()> sceneProvider);

} // namespace HIKARI::EDITOR
