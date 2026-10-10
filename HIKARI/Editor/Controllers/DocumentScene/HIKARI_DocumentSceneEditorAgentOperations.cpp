#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Assets/Models/HIKARI_ModelBounds.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Agent/HIKARI_AgentProjectFiles.h"
#include "Editor/Authoring/HIKARI_SceneObjectObservation.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Commands/HIKARI_EditorOperationSearch.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include <algorithm>
#include <filesystem>
#include <numbers>
#include <string>
#include <vector>
#include <utility>

namespace HIKARI {
namespace {

constexpr size_t kMaximumBatchSteps = 64u;
constexpr size_t kMaximumDescribedSelection = 16u;

nlohmann::json Vec3Json(const MATH::Vec3& value) { return nlohmann::json::array({value.x, value.y, value.z}); }

EDITOR::EditorOperationResult BatchFailure(std::string_view code, std::string message, size_t step,
                                           const std::string& operation = {}) {
    EDITOR::EditorOperationResult result = EDITOR::OperationFailure(code, std::move(message));
    result.data = {{"step", step}};
    if (!operation.empty())
        result.data["operation"] = operation;
    return result;
}

const nlohmann::json& StepParameters(const nlohmann::json& step) {
    static const nlohmann::json kEmpty = nlohmann::json::object();
    const auto found = step.find("parameters");
    return found != step.end() ? *found : kEmpty;
}

} // namespace

void DocumentSceneEditorController::RegisterAgentOperations() {
    using EDITOR::EditorOperationParameterType;
    EDITOR::EditorOperationDefinition state{
        "editor.state",
        "Describe the live editor in one call: project, open scene, active workspace document, selection with "
        "components, editor camera, Play state, unsaved documents, background authoring sessions and pending "
        "approvals. Read it before planning edits.",
        "general",
        {},
        [this](const nlohmann::json&) { return DescribeEditorState(); }};
    state.permission = EDITOR::EditorOperationPermission::ReadEditorState;
    state.entryPoint = true;
    state.searchTerms = {"state", "selection", "context", "camera", "状态", "选中", "现场"};
    operationRegistry_.Register(std::move(state), EDITOR::EditorOperationKind::Query, false);

    EDITOR::EditorOperationDefinition batch{
        "operations.batch",
        "Run up to 64 registered operations in order in one call. Scene document edits become one undo step. The "
        "first failure stops the batch and, unless rollbackOnError is false, undoes the Scene edits it made; edits in "
        "other documents (materials, VFX, sequences) stay. Steps that need user approval (save, import, external "
        "processes) must be called on their own.",
        "general",
        {{"steps",
          EditorOperationParameterType::Json,
          true,
          "1 to 64 steps, each {\"operation\": operation id, \"parameters\": {...}}.",
          {{"type", "array"},
           {"items",
            {{"type", "object"},
             {"required", {"operation"}},

             {"properties", {{"operation", {{"type", "string"}}}, {"parameters", {{"type", "object"}}}}}}}}},
         {"rollbackOnError", EditorOperationParameterType::Bool, false,
          "Undo this batch's Scene edits when a step fails (default true)."}},
        [this](const nlohmann::json& parameters) { return ExecuteOperationBatch(parameters); }};
    batch.permission = EDITOR::EditorOperationPermission::ModifyEditorState;
    batch.entryPoint = true;
    // 人の画面は操作を直接呼ぶ。一括は智能体の往復を減らすための入口。
    batch.exposureMask = EDITOR::EditorOperationExposureMask::Agent;
    batch.searchTerms = {"batch", "multiple", "sequence of edits", "transaction", "批量", "一次", "多个操作"};
    operationRegistry_.Register(std::move(batch), EDITOR::EditorOperationKind::Mutation, false);

    EDITOR::EditorOperationDefinition search{
        "operations.search",
        "Find operations by purpose. Returns ranked matches with their parameters. With neither keyword nor "
        "workspace it returns only the operation count per workspace.",
        "general",
        {{"keyword", EditorOperationParameterType::String, false, "Purpose, operation name or search terms"},
         {"workspace", EditorOperationParameterType::String, false, "Workspace id"},
         {"includeParameters", EditorOperationParameterType::Bool, false, "Include parameter schemas (default true)"},
         {"offset", EditorOperationParameterType::Int, false, "Next page offset from a previous result"},
         {"limit", EditorOperationParameterType::Int, false, "Results per page, 1 to 32 (default 12)"}},
        [this](const nlohmann::json& parameters) {
            EDITOR::EditorOperationSearchQuery query;
            query.keyword = parameters.value("keyword", std::string{});
            query.workspace = parameters.value("workspace", std::string{});
            query.includeParameters = parameters.value("includeParameters", true);
            query.offset = static_cast<size_t>((std::max)(0, parameters.value("offset", 0)));
            query.limit = static_cast<size_t>(std::clamp(parameters.value("limit", 12), 1, 32));
            return EDITOR::OperationOk(
                "Matching operations.",
                EDITOR::SearchEditorOperations(operationRegistry_, EDITOR::EditorOperationExposureMask::Agent, query));
        }};
    search.permission = EDITOR::EditorOperationPermission::ReadEditorState;
    search.entryPoint = true;
    search.exposureMask = EDITOR::EditorOperationExposureMask::Agent;
    search.searchTerms = {"find", "capabilities", "help", "查找", "能力"};
    operationRegistry_.Register(std::move(search), EDITOR::EditorOperationKind::Query, false);

    EDITOR::EditorOperationDefinition describe{
        "operations.describe",
        "Describe one operation: summary, permission, kind and the full parameter schema.",
        "general",
        {{"operation", EditorOperationParameterType::String, true, "Operation id"}},
        [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
            const std::string id = parameters.at("operation").get<std::string>();
            const EDITOR::EditorOperationDefinition* definition = operationRegistry_.Find(id);
            if (definition == nullptr || !EDITOR::HasEditorOperationExposure(
                                             definition->exposureMask, EDITOR::EditorOperationExposureMask::Agent))
                return EDITOR::OperationFailure("UNKNOWN_OPERATION", "No agent operation named " + id + ".");
            return EDITOR::OperationOk(definition->summary, EDITOR::DescribeEditorOperationCompact(*definition, true));
        }};
    describe.permission = EDITOR::EditorOperationPermission::ReadEditorState;
    describe.exposureMask = EDITOR::EditorOperationExposureMask::Agent;
    describe.searchTerms = {"parameters", "schema", "help", "参数"};
    operationRegistry_.Register(std::move(describe), EDITOR::EditorOperationKind::Query, false);

    EDITOR::EditorOperationDefinition agentFiles{
        "project.agent_files.write",
        "Write AGENTS.md, CLAUDE.md and .mcp.json in the project root so Claude Code and Codex can connect through "
        "hikari.exe. Keeps the conventions section of an existing AGENTS.md.",
        "general",
        {},
        [this](const nlohmann::json&) -> EDITOR::EditorOperationResult {
            if (aiOperationScene_ == nullptr)
                return EDITOR::OperationFailure("NO_SCENE", "Open a project first.");
            const std::filesystem::path root = aiOperationScene_->GetAssetDatabase().GetProjectRoot();
            std::vector<std::filesystem::path> written;
            std::string error;
            if (!EDITOR::AGENT::WriteAgentProjectFiles(
                    root, EDITOR::AGENT::DetectAgentProjectFileInputs(TEXT::PathToUtf8(root.filename())), written,
                    error))
                return EDITOR::OperationFailure("WRITE_FAILED", std::move(error));
            nlohmann::json files = nlohmann::json::array();
            for (const std::filesystem::path& path : written)
                files.push_back(TEXT::PathToUtf8(path.filename()));
            return EDITOR::OperationOk(TEXT::Localize("settings.aiBridge.agentFilesWritten"), {{"files", files}});
        }};
    agentFiles.permission = EDITOR::EditorOperationPermission::ModifyProjectFiles;
    agentFiles.searchTerms = {"AGENTS.md", "CLAUDE.md", "mcp", "agent setup", "智能体说明"};
    operationRegistry_.Register(std::move(agentFiles), EDITOR::EditorOperationKind::Mutation, false);
}

EDITOR::EditorOperationResult DocumentSceneEditorController::DescribeEditorState() {
    nlohmann::json state = nlohmann::json::object();
    const EDITOR::EditorWorkspaceId active = workspaceHost_.GetActive();
    state["workspace"] = {{"active", EDITOR::EditorWorkspaceOperationId(active)}};
    if (nlohmann::json document = DescribeWorkspaceDocument(active, true); !document.is_null())
        state["workspace"]["document"] = std::move(document);
    state["agent"] = {
        {"accessMode", std::string(TEXT::EnumToName(EDITOR::AGENT::kAgentAccessModeNames, agentHost_.AccessMode()))},
        {"pendingApprovals", agentHost_.PendingApprovals().size()}};
    DocumentSceneBase* scene = aiOperationScene_;
    if (scene == nullptr) {
        state["scene"] = nullptr;
        return EDITOR::OperationOk("No scene is open.", std::move(state));
    }
    state["project"] = {{"root", TEXT::PathToUtf8(scene->GetAssetDatabase().GetProjectRoot())}};
    const SceneDocument& document = scene->GetSceneDocument();
    nlohmann::json sceneState{
        {"path", scene->GetScenePath()},         {"objects", document.objects.size()},
        {"dirty", context_.sceneDirty},          {"documentRevision", scene->GetSceneDocumentRevision()},
        {"canUndo", documentHistory_.CanUndo()}, {"canRedo", documentHistory_.CanRedo()}};
    if (const std::string* label = documentHistory_.GetUndoLabel())
        sceneState["undoLabel"] = *label;
    state["scene"] = std::move(sceneState);

    const std::vector<SceneObjectId>& selectedIds = context_.selection.GetSelectedObjectIds();
    nlohmann::json selected = nlohmann::json::array();
    for (SceneObjectId id : selectedIds) {
        if (selected.size() >= kMaximumDescribedSelection)
            break;
        const auto object = std::find_if(document.objects.begin(), document.objects.end(),
                                         [id](const SceneObjectData& candidate) { return candidate.id == id; });
        if (object == document.objects.end())
            continue;
        nlohmann::json components = nlohmann::json::array();
        for (const SceneComponentData& component : object->components)
            components.push_back({{"type", component.type}, {"componentId", component.id.value}});
        nlohmann::json entry{{"objectId", id.value}, {"name", object->name}, {"components", std::move(components)}};
        if (object->parent)
            entry["parentId"] = object->parent->value;
        if (const Bounds bounds = EDITOR::ObserveSceneObjectBounds(*scene, id); BOUNDS::IsUsable(bounds))
            entry["worldBounds"] = {{"min", Vec3Json(bounds.min)}, {"max", Vec3Json(bounds.max)}};
        selected.push_back(std::move(entry));
    }
    state["selection"] = {{"activeObjectId", context_.selection.GetActiveObjectId().value},
                          {"count", selectedIds.size()},
                          {"objects", std::move(selected)}};
    if (selectedIds.size() > kMaximumDescribedSelection)
        state["selection"]["truncated"] = true;

    const Camera3D& camera = scene->GetCamera();
    state["camera"] = {{"position", Vec3Json(camera.GetPosition())},
                       {"target", Vec3Json(camera.GetTarget())},
                       {"orthographic", camera.IsOrthographic()},
                       {"fovYDegrees", camera.GetFovYRad() * 180.0f / std::numbers::pi_v<float>}};
    state["play"] = {{"active", scene->IsRuntimePlayActive()}};
    state["unsavedProjectChanges"] = HasKnownUnsavedProjectChanges(*scene);
    state["authoringSessions"] = authoringSessions_.Snapshot(true);
    return EDITOR::OperationOk("Editor state.", std::move(state));
}

EDITOR::EditorOperationResult DocumentSceneEditorController::ExecuteOperationBatch(const nlohmann::json& parameters) {
    const nlohmann::json& steps = parameters.at("steps");
    if (steps.empty() || steps.size() > kMaximumBatchSteps)
        return EDITOR::OperationFailure("INVALID_STEPS", "steps must contain 1 to 64 operations.");
    // 何も実行しないうちに、未知の操作・入れ子・承認が要る操作を拒否する。
    for (size_t index = 0u; index < steps.size(); ++index) {
        const std::string operation = steps[index].at("operation").get<std::string>();
        if (operation == "operations.batch")
            return BatchFailure("NESTED_BATCH", "operations.batch cannot contain another batch.", index, operation);
        const EDITOR::EditorOperationDefinition* definition = operationRegistry_.Find(operation);
        if (definition == nullptr)
            return BatchFailure("UNKNOWN_OPERATION",
                                "Unknown operation in step " + std::to_string(index) + ": " + operation, index,
                                operation);
        if (EDITOR::RequiresUserApproval(definition->permission))
            return BatchFailure("STEP_REQUIRES_APPROVAL",
                                "Step " + std::to_string(index) + " (" + operation +
                                    ") needs user approval; call it on its own after the batch.",
                                index, operation);
    }

    const bool fromAgent = executingAgentAction_;
    const bool rollbackOnError = parameters.value("rollbackOnError", true);
    const size_t depthBefore = documentHistory_.GetUndoDepth();
    nlohmann::json results = nlohmann::json::array();
    std::optional<size_t> failedStep;
    std::string failureMessage;
    documentHistory_.BeginUndoGroup();
    for (size_t index = 0u; index < steps.size(); ++index) {
        const std::string operation = steps[index]["operation"].get<std::string>();
        const nlohmann::json& stepParameters = StepParameters(steps[index]);
        const EDITOR::EditorOperationResult result = fromAgent ? ExecuteAgentAction(operation, stepParameters)
                                                               : operationRegistry_.Execute(operation, stepParameters);
        nlohmann::json entry{{"step", index},
                             {"operation", operation},
                             {"ok", result.ok},
                             {"code", result.code},
                             {"message", result.message}};
        if (!result.data.empty())
            entry["data"] = result.data;
        if (!result.details.empty())
            entry["details"] = result.details;
        const bool ok = entry["ok"].get<bool>();
        if (!ok)
            failureMessage = entry["message"].get<std::string>();
        results.push_back(std::move(entry));
        if (!ok) {
            failedStep = index;
            break;
        }
    }
    documentHistory_.EndUndoGroup();

    bool rolledBack = false;
    // 一括の記録だけを戻す。智能体の一回の依頼の組（撤销这一轮）に入っていても、前の手順は残す。
    if (failedStep && rollbackOnError && aiOperationScene_ != nullptr &&
        documentHistory_.GetUndoDepth() > depthBefore) {
        ApplyHistoryResult(*aiOperationScene_,
                           documentHistory_.UndoTo(aiOperationScene_->GetSceneDocument(), depthBefore), false);
        rolledBack = true;
    }
    nlohmann::json data{{"steps", std::move(results)},
                        {"completed", failedStep ? *failedStep : steps.size()},
                        {"rolledBack", rolledBack}};
    if (!failedStep)
        return EDITOR::OperationOk("Completed " + std::to_string(steps.size()) + " steps.", std::move(data));
    data["failedStep"] = *failedStep;
    EDITOR::EditorOperationResult result = EDITOR::OperationFailure(
        "BATCH_STEP_FAILED", "Step " + std::to_string(*failedStep) + " failed: " + failureMessage +
                                 (rolledBack ? " The Scene edits from this batch were undone." : ""));
    result.data = std::move(data);
    return result;
}

} // namespace HIKARI
