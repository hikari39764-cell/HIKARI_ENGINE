#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialOperationSupport.h"

#include <utility>

namespace HIKARI::EDITOR::MODEL_MATERIAL_OPERATIONS {

EditorOperationParameter MakeOperationParameter(
    std::string name,
    EditorOperationParameterType type,
    bool required,
    std::string description) {
  EditorOperationParameter parameter{};
  parameter.name = std::move(name);
  parameter.type = type;
  parameter.required = required;
  parameter.description = std::move(description);
  return parameter;
}

bool RequireScene(
    const std::function<DocumentSceneBase*()>& sceneProvider,
    EditorOperationResult& result,
    DocumentSceneBase*& outScene) {
  outScene = sceneProvider();
  if (outScene != nullptr) {
    return true;
  }
  result.code = "NO_SCENE";
  result.message = "当前没有可用的场景上下文。";
  return false;
}

} // namespace HIKARI::EDITOR::MODEL_MATERIAL_OPERATIONS
