#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

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

enum class EditorOperationSessionDispatch : uint8_t { Operation, Preview };

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
    // 追加制約や構造化値の形は宣言側が所有し、検証とヘルプで共有する。
    nlohmann::json schema = nlohmann::json::object();
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
    // Project asset discovery does not read or mutate a workspace document.
    bool requiresDocumentSession = true;
    std::vector<std::string> searchTerms{};
    // 文書を置換する操作はバックグラウンドから非表示の確認ダイアログを開かない。
    bool requiresCleanDocument = false;
    bool entryPoint = false;
    EditorOperationSessionDispatch sessionDispatch = EditorOperationSessionDispatch::Operation;
};

// UI / AI / Bridge の能力説明とエラーは同じ実行定義から生成する。
nlohmann::json DescribeEditorOperation(const EditorOperationDefinition& definition);

// Unified operation bus: human UI, AI actions and the local bridge all call
// the same registered operations instead of maintaining parallel dispatchers.
class EditorOperationRegistry {
public:
    using TargetExecutor = std::function<EditorOperationResult(
        const std::string&, const nlohmann::json&)>;
    void SetTargetExecutor(TargetExecutor executor) { targetExecutor_ = std::move(executor); }
    void SetTargetSelector(std::function<std::string(const std::string&)> selector) { targetSelector_ = std::move(selector); }
    EditorOperationRegistrationResult Register(
        EditorOperationDefinition definition);
    EditorOperationRegistrationResult Register(EditorOperationDefinition definition,
        EditorOperationKind kind, bool requiresDocumentSession = true,
        EditorOperationSessionDispatch dispatch = EditorOperationSessionDispatch::Operation) {
        definition.kind = kind;
        definition.requiresDocumentSession = requiresDocumentSession;
        definition.sessionDispatch = dispatch;
        return Register(std::move(definition));
    }
    bool IsRegistered(const std::string& id) const noexcept;
    uint64_t Revision() const noexcept { return revision_; }
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
    TargetExecutor targetExecutor_{};
    std::function<std::string(const std::string&)> targetSelector_{};
    std::unordered_map<std::string, EditorOperationDefinition> operations_{};
    uint64_t revision_ = 0;
};

} // namespace HIKARI::EDITOR
