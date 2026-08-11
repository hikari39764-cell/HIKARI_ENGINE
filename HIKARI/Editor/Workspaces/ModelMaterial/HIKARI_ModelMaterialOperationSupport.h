#pragma once

#include <functional>
#include <string>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR::MODEL_MATERIAL_OPERATIONS {

EditorOperationParameter MakeOperationParameter(
    std::string name,
    EditorOperationParameterType type,
    bool required,
    std::string description);

bool RequireScene(
    const std::function<DocumentSceneBase*()>& sceneProvider,
    EditorOperationResult& result,
    DocumentSceneBase*& outScene);

} // namespace HIKARI::EDITOR::MODEL_MATERIAL_OPERATIONS
