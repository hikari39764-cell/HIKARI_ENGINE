#pragma once
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include <string_view>

namespace HIKARI::EDITOR {
    using SceneFolderExecutor = std::function<EditorOperationResult(std::string_view, const nlohmann::json&)>;
    void RegisterSceneFolderOperations(EditorOperationRegistry& registry, SceneFolderExecutor execute);
}
