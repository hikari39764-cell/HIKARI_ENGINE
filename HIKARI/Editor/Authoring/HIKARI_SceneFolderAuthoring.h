#pragma once
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/History/HIKARI_EditorDocumentHistory.h"
#include <memory>
#include <string_view>

namespace HIKARI::EDITOR {
    struct SceneFolderEdit {
        EditorOperationResult result{};
        std::unique_ptr<IEditorDocumentCommand> command{};
    };
    bool IsValidSceneFolderPath(std::string_view path) noexcept;
    std::vector<std::string> CollectSceneFolderPaths(const SceneDocument& document);
    SceneFolderEdit ApplySceneFolderOperation(SceneDocument& document,
        std::string_view operation, const nlohmann::json& parameters);
}
