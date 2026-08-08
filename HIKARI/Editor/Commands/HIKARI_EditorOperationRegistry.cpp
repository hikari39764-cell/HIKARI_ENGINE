#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

#include <algorithm>

namespace HIKARI::EDITOR {

void EditorOperationRegistry::Register(
    EditorOperationDefinition definition,
    bool longRunning) {
    if (definition.id.empty() || !definition.execute) {
        return;
    }
    definition.longRunning = longRunning;
    operations_[definition.id] = std::move(definition);
}

bool EditorOperationRegistry::IsRegistered(
    const std::string& id) const noexcept {
    return operations_.find(id) != operations_.end();
}

EditorOperationResult EditorOperationRegistry::Execute(
    const std::string& id,
    const nlohmann::json& parameters) const {
    const auto found = operations_.find(id);
    if (found == operations_.end()) {
        EditorOperationResult result{};
        result.code = "OPERATION_NOT_FOUND";
        result.message = "Unregistered editor operation: " + id;
        return result;
    }
    return found->second.execute(parameters);
}

EditorOperationResult EditorOperationRegistry::Execute(
    const std::string& id) const {
    return Execute(id, nlohmann::json::object());
}

std::vector<std::string> EditorOperationRegistry::RegisteredIds() const {
    std::vector<std::string> ids{};
    ids.reserve(operations_.size());
    for (const auto& [id, definition] : operations_) {
        (void)definition;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

const EditorOperationDefinition* EditorOperationRegistry::Find(
    const std::string& id) const noexcept {
    const auto found = operations_.find(id);
    return found != operations_.end()
        ? &found->second
        : nullptr;
}

std::vector<const EditorOperationDefinition*>
EditorOperationRegistry::QueryWorkspace(
    const std::string& workspace) const {
    std::vector<const EditorOperationDefinition*> definitions{};
    for (const auto& [id, definition] : operations_) {
        (void)id;
        if (definition.workspace == workspace) {
            definitions.push_back(&definition);
        }
    }
    std::sort(
        definitions.begin(),
        definitions.end(),
        [](const EditorOperationDefinition* lhs,
           const EditorOperationDefinition* rhs) {
            return lhs->id < rhs->id;
        });
    return definitions;
}

} // namespace HIKARI::EDITOR
