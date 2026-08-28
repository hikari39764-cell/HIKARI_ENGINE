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

enum class EditorOperationKind : uint8_t {
    Legacy,
    Query,
    Mutation,
    Session,
};

enum class EditorOperationExposureMask : uint8_t {
    None = 0,
    Human = 1u << 0u,
    BuiltInAi = 1u << 1u,
    LocalBridge = 1u << 2u,
    Automation = 1u << 3u,
    LegacyDefault =
        (1u << 0u) | (1u << 1u) | (1u << 2u),
};

constexpr EditorOperationExposureMask operator|(
    EditorOperationExposureMask lhs,
    EditorOperationExposureMask rhs) noexcept {

    return static_cast<EditorOperationExposureMask>(
        static_cast<uint8_t>(lhs) |
        static_cast<uint8_t>(rhs));
}

constexpr bool HasEditorOperationExposure(
    EditorOperationExposureMask value,
    EditorOperationExposureMask exposure) noexcept {

    return (static_cast<uint8_t>(value) &
        static_cast<uint8_t>(exposure)) != 0u;
}

enum class EditorOperationPermission : uint8_t {
    Legacy,
    ReadEditorState,
    ModifyEditorState,
    ModifyProjectFiles,
    ExecuteExternalProcess,
};

enum class EditorOperationRisk : uint8_t {
    Legacy,
    Low,
    Medium,
    High,
    Destructive,
};

enum class EditorOperationRegistrationCode : uint8_t {
    Unspecified,
    Registered,
    MissingId,
    MissingSummary,
    MissingWorkspace,
    MissingExecute,
    DuplicateId,
};

struct EditorOperationRegistrationResult {
    bool ok = false;
    EditorOperationRegistrationCode code =
        EditorOperationRegistrationCode::Unspecified;
    std::string message{};
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
    // Existing operations retain their current discovery behavior until each
    // registrar is migrated to an explicit Schema v2 classification.
    EditorOperationKind kind = EditorOperationKind::Legacy;
    EditorOperationExposureMask exposureMask =
        EditorOperationExposureMask::LegacyDefault;
    EditorOperationPermission permission =
        EditorOperationPermission::Legacy;
    EditorOperationRisk risk = EditorOperationRisk::Legacy;
};

// Unified operation bus: human UI, AI actions and the local bridge all call
// the same registered operations instead of maintaining parallel dispatchers.
class EditorOperationRegistry {
public:
    EditorOperationRegistrationResult Register(
        EditorOperationDefinition definition);
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
