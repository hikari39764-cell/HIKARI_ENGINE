#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <json.hpp>

namespace HIKARI::EDITOR {

struct EditorOperationResult {
    bool ok = false;
    std::string code{};
    std::string message{};
    std::vector<std::string> details{};
    std::vector<std::string> hints{};
};

struct EditorOperationDefinition {
    std::string id{};
    std::string summary{};
    std::function<EditorOperationResult(const nlohmann::json&)> execute{};
};

// Unified operation bus: human UI, AI actions and the local bridge all call
// the same registered operations instead of maintaining parallel dispatchers.
class EditorOperationRegistry {
public:
    void Register(EditorOperationDefinition definition);
    bool IsRegistered(const std::string& id) const noexcept;
    EditorOperationResult Execute(
        const std::string& id,
        const nlohmann::json& parameters) const;
    std::vector<std::string> RegisteredIds() const;

private:
    std::unordered_map<std::string, EditorOperationDefinition> operations_{};
};

} // namespace HIKARI::EDITOR
