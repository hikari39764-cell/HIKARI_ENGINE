#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <json.hpp>

namespace HIKARI::EDITOR {

enum class EditorOperationParameterType : uint8_t {
    String,
    Float,
    Int,
    Bool,
    Json,
};

enum class EditorOperationExecutionPolicy : uint8_t {
    MainThread,
    WorkerThread,
};

struct EditorOperationParameter {
    std::string name{};
    EditorOperationParameterType type =
        EditorOperationParameterType::String;
    bool required = false;
    std::string description{};
};

struct EditorOperationResult {
    bool ok = false;
    std::string code{};
    std::string message{};
    nlohmann::json data = nlohmann::json::object();
    std::vector<std::string> details{};
    std::vector<std::string> hints{};
};

struct EditorOperationDefinition {
    std::string id{};
    std::string summary{};
    // Owning workspace tag ("scene", "model_material", "model_collision",
    // "animation_state_machine", "cinematics", "general").
    std::string workspace{};
    std::vector<EditorOperationParameter> parameters{};
    std::function<EditorOperationResult(const nlohmann::json&)> execute{};
    EditorOperationExecutionPolicy executionPolicy =
        EditorOperationExecutionPolicy::MainThread;
};

// Unified operation bus: human UI, AI actions and the local bridge all call
// the same registered operations instead of maintaining parallel dispatchers.
class EditorOperationRegistry {
public:
    void Register(EditorOperationDefinition definition);
    bool IsRegistered(const std::string& id) const noexcept;
    EditorOperationResult Validate(
        const EditorOperationDefinition& definition,
        const nlohmann::json& parameters) const;
    EditorOperationResult Execute(
        const std::string& id,
        const nlohmann::json& parameters) const;
    // Convenience for human UI command bindings that need no parameters.
    EditorOperationResult Execute(const std::string& id) const;
    std::vector<std::string> RegisteredIds() const;
    const EditorOperationDefinition* Find(
        const std::string& id) const noexcept;
    std::vector<const EditorOperationDefinition*> QueryWorkspace(
        const std::string& workspace) const;

private:
    std::unordered_map<std::string, EditorOperationDefinition> operations_{};
};

} // namespace HIKARI::EDITOR
