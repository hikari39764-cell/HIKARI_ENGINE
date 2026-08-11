#pragma once

#include <functional>

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

class EditorOperationRegistry;
class ModelMaterialWorkspaceController;

// Registers project Material asset discovery and creation. These operations
// do not select or bind model material slots.
void RegisterModelMaterialAssetOperations(
    EditorOperationRegistry& registry,
    ModelMaterialWorkspaceController& controller,
    std::function<DocumentSceneBase*()> sceneProvider);

} // namespace HIKARI::EDITOR
