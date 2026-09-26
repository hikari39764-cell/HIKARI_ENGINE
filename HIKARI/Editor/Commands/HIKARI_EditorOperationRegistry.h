#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

#include <json.hpp>

#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI::EDITOR {

enum class EditorOperationParameterType : uint8_t {
    String,
    Float,
    Int,
    Bool,
    Json,
};

// 検証メッセージ・能力説明・AI への能力一覧で共通の型名。
inline constexpr TEXT::EnumName<EditorOperationParameterType> kEditorOperationParameterTypeNames[] = {
    {EditorOperationParameterType::String, "string"}, {EditorOperationParameterType::Float, "number"},
    {EditorOperationParameterType::Int, "integer"},   {EditorOperationParameterType::Bool, "boolean"},
    {EditorOperationParameterType::Json, "json"},
};

enum class EditorOperationExecutionPolicy : uint8_t {
    MainThread,
    WorkerThread,
};

// Unspecified は登録時に拒否する。各操作は必ず分類を宣言する。
enum class EditorOperationKind : uint8_t {
    Unspecified,
    Query,
    Mutation,
    Session,
};

inline constexpr TEXT::EnumName<EditorOperationKind> kEditorOperationKindNames[] = {
    {EditorOperationKind::Query, "query"},
    {EditorOperationKind::Mutation, "mutation"},
    {EditorOperationKind::Session, "session"},
};

enum class EditorOperationSessionDispatch : uint8_t { Operation, Preview };

enum class EditorOperationExposureMask : uint8_t {
    None = 0,
    Human = 1u << 0u,
    BuiltInAi = 1u << 1u,
    LocalBridge = 1u << 2u,
    Automation = 1u << 3u,
    Default = (1u << 0u) | (1u << 1u) | (1u << 2u),
};

constexpr EditorOperationExposureMask operator|(EditorOperationExposureMask lhs,
                                                EditorOperationExposureMask rhs) noexcept {
    return static_cast<EditorOperationExposureMask>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr bool HasEditorOperationExposure(EditorOperationExposureMask value,
                                          EditorOperationExposureMask exposure) noexcept {
    return (static_cast<uint8_t>(value) & static_cast<uint8_t>(exposure)) != 0u;
}

enum class EditorOperationPermission : uint8_t {
    Unspecified,
    ReadEditorState,
    ModifyEditorState,
    ModifyProjectFiles,
    ExecuteExternalProcess,
};

inline constexpr TEXT::EnumName<EditorOperationPermission> kEditorOperationPermissionNames[] = {
    {EditorOperationPermission::Unspecified, "unspecified"},
    {EditorOperationPermission::ReadEditorState, "readEditorState"},
    {EditorOperationPermission::ModifyEditorState, "modifyEditorState"},
    {EditorOperationPermission::ModifyProjectFiles, "modifyProjectFiles"},
    {EditorOperationPermission::ExecuteExternalProcess, "executeExternalProcess"},
};

// AI・外部エージェントからの呼び出しでは、プロジェクトファイルの変更と外部プロセスの起動をユーザーの承認後に実行する。
constexpr bool RequiresUserApproval(EditorOperationPermission permission) noexcept {
    return permission == EditorOperationPermission::ModifyProjectFiles ||
           permission == EditorOperationPermission::ExecuteExternalProcess;
}

enum class EditorOperationRegistrationCode : uint8_t {
    Unspecified,
    Registered,
    MissingId,
    MissingSummary,
    MissingWorkspace,
    MissingExecute,
    MissingKind,
    DuplicateId,
};

struct EditorOperationRegistrationResult {
    bool ok = false;
    EditorOperationRegistrationCode code = EditorOperationRegistrationCode::Unspecified;
    std::string message{};
};

struct EditorOperationParameter {
    std::string name{};
    EditorOperationParameterType type = EditorOperationParameterType::String;
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

// Ignores only float storage roundoff; integer values and all structure remain exact.
bool EquivalentOperationValue(const nlohmann::json& requested, const nlohmann::json& applied);

struct EditorOperationDefinition {
    std::string id{};
    std::string summary{};
    // Owning workspace tag ("scene", "model_material", "model_collision",
    // "animation_state_machine", "cinematics", "general").
    std::string workspace{};
    std::vector<EditorOperationParameter> parameters{};
    std::function<EditorOperationResult(const nlohmann::json&)> execute{};
    EditorOperationExecutionPolicy executionPolicy = EditorOperationExecutionPolicy::MainThread;
    EditorOperationKind kind = EditorOperationKind::Unspecified;
    EditorOperationExposureMask exposureMask = EditorOperationExposureMask::Default;
    EditorOperationPermission permission = EditorOperationPermission::Unspecified;
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
    using TargetExecutor = std::function<EditorOperationResult(const std::string&, const nlohmann::json&)>;
    void SetTargetExecutor(TargetExecutor executor) { targetExecutor_ = std::move(executor); }
    void SetTargetSelector(std::function<std::string(const std::string&)> selector) {
        targetSelector_ = std::move(selector);
    }
    EditorOperationRegistrationResult Register(EditorOperationDefinition definition);
    EditorOperationRegistrationResult Register(
        EditorOperationDefinition definition, EditorOperationKind kind, bool requiresDocumentSession = true,
        EditorOperationSessionDispatch dispatch = EditorOperationSessionDispatch::Operation) {
        definition.kind = kind;
        definition.requiresDocumentSession = requiresDocumentSession;
        definition.sessionDispatch = dispatch;
        return Register(std::move(definition));
    }
    bool IsRegistered(const std::string& id) const noexcept;
    uint64_t Revision() const noexcept { return revision_; }
    EditorOperationResult Validate(const EditorOperationDefinition& definition, const nlohmann::json& parameters) const;
    EditorOperationResult Execute(const std::string& id, const nlohmann::json& parameters) const;
    // Convenience for human UI command bindings that need no parameters.
    EditorOperationResult Execute(const std::string& id) const;
    std::vector<std::string> RegisteredIds() const;
    const EditorOperationDefinition* Find(const std::string& id) const noexcept;

private:
    TargetExecutor targetExecutor_{};
    std::function<std::string(const std::string&)> targetSelector_{};
    std::unordered_map<std::string, EditorOperationDefinition> operations_{};
    uint64_t revision_ = 0;
};

} // namespace HIKARI::EDITOR
