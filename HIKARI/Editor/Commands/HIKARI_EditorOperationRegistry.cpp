#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

#include <algorithm>

namespace HIKARI::EDITOR {

void EditorOperationRegistry::Register(
    EditorOperationDefinition definition) {
    if (definition.id.empty() || !definition.execute) {
        return;
    }
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

} // namespace HIKARI::EDITOR
