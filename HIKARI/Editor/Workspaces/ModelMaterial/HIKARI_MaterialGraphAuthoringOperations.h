#pragma once
#include <functional>
namespace HIKARI {
class DocumentSceneBase;
}
namespace HIKARI::EDITOR {
class EditorOperationRegistry;
class ModelMaterialWorkspaceController;
void RegisterMaterialGraphAuthoringOperations(EditorOperationRegistry& registry,
                                              ModelMaterialWorkspaceController& controller,
                                              std::function<DocumentSceneBase*()> sceneProvider);
} // namespace HIKARI::EDITOR
