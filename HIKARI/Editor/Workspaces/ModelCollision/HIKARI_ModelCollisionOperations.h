#pragma once

#include <functional>

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

class EditorOperationRegistry;
class ModelCollisionWorkspaceController;

// Registers Model Collision workspace operations on the unified editor
// operation bus. Human UI, AI actions and the local bridge all reach the
// workspace through these operations.
void RegisterModelCollisionOperations(EditorOperationRegistry& registry, ModelCollisionWorkspaceController& controller,
                                      std::function<DocumentSceneBase*()> sceneProvider);

} // namespace HIKARI::EDITOR
