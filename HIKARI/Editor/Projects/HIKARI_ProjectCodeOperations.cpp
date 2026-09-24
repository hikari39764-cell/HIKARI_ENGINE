#include "Editor/Projects/HIKARI_ProjectCodeJob.h"
#include "Editor/Projects/HIKARI_ProjectCodeWorkspace.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Project/HIKARI_ProjectContext.h"

namespace HIKARI::EDITOR {
void RegisterProjectCodeOperations(EditorOperationRegistry& registry, ProjectCodeJob& job,
                                   std::function<bool()> canApply) {
    using P = EditorOperationParameterType;
    auto add = [&](std::string id, std::string summary, std::vector<EditorOperationParameter> parameters,
                   std::function<EditorOperationResult(const nlohmann::json&)> action, EditorOperationKind kind,
                   EditorOperationPermission permission) {
        EditorOperationDefinition definition{
            std::move(id), std::move(summary), "project_code", std::move(parameters),
            [action = std::move(action)](const nlohmann::json& p) {
                try {
                    if (!GetProjectContext().IsDescriptorBacked())
                        return EditorOperationResult{false, "NO_PROJECT", "Open a game project first."};
                    return action(p);
                } catch (const std::exception& error) {
                    return EditorOperationResult{false, "CODE_TASK_FAILED", error.what()};
                }
            }};
        definition.kind = kind;
        definition.requiresDocumentSession = false;
        definition.permission = permission;
        definition.risk = kind == EditorOperationKind::Query ? EditorOperationRisk::Low : EditorOperationRisk::Medium;
        registry.Register(std::move(definition));
    };
    add(
        "project.code.files",
        "List project-owned source paths and current SHA256 versions; engine source is not exposed.", {},
        [](const nlohmann::json&) {
            return EditorOperationResult{true, "OK", "Project code snapshot.",
                                         PROJECT_CODE::Snapshot(GetProjectContext().GetRoot())};
        },
        EditorOperationKind::Query, EditorOperationPermission::ReadEditorState);
    add(
        "project.code.read",
        "Read one project code file and its version. Missing files return an empty SHA256 for creating them.",
        {{"path", P::String, true,
          "Project-relative Source, Tests, Tools or cmake file, or root CMake/Project manifest."}},
        [](const nlohmann::json& p) {
            return EditorOperationResult{
                true, "OK", "Project source.",
                PROJECT_CODE::ReadFile(GetProjectContext().GetRoot(), p.at("path").get<std::string>())};
        },
        EditorOperationKind::Query, EditorOperationPermission::ReadEditorState);
    add(
        "project.code.sdk",
        "Read a public SDK header. Start with Project/GameComponent.h, Project/WorldView.h and Project/RuntimeRegistration.h. Report a capability gap when these APIs are insufficient.",
        {{"path", P::String, true, "Header relative to SDK include/HIKARI, for example Project/GameComponent.h."}},
        [](const nlohmann::json& p) {
            return EditorOperationResult{
                true, "OK", "Public SDK header.",
                PROJECT_CODE::ReadSdkHeader(ProjectCodeJob::SdkRoot(), p.at("path").get<std::string>())};
        },
        EditorOperationKind::Query, EditorOperationPermission::ReadEditorState);
    add(
        "project.code.prepare",
        "Stage version-checked project source edits and build them in a hidden worker. Original source and modules remain unchanged. No automatic activation.",
        {{"edits", P::Json, true,
          "1..64 {path, baseSha256, content}; read each current file first. Optional project Tests/HikariVerification.json declares windowless behavior checks."}},
        [&job](const nlohmann::json& p) {
            std::string message;
            const bool ok = job.Start(p.at("edits"), message);
            return EditorOperationResult{ok, ok ? "STARTED" : "REJECTED", message, job.Status()};
        },
        EditorOperationKind::Session, EditorOperationPermission::ExecuteExternalProcess);
    add(
        "project.code.status",
        "Report source, compilation, behavior validation and activation separately. Compilation does not prove visual or gameplay correctness.",
        {},
        [&job](const nlohmann::json&) {
            job.Poll();
            return EditorOperationResult{true, "OK", "Project code task status.", job.Status()};
        },
        EditorOperationKind::Query, EditorOperationPermission::ReadEditorState);
    add(
        "project.code.diff",
        "List changed files and conflict state. Supply one changed path to read current and candidate source for review.",
        {{"path", P::String, false, "Optional changed project-relative source path; omit to list the candidate."}},
        [&job](const nlohmann::json& p) {
            return EditorOperationResult{true, "OK", "Candidate comparison.", job.Diff(p.value("path", std::string{}))};
        },
        EditorOperationKind::Query, EditorOperationPermission::ReadEditorState);
    add(
        "project.code.cancel",
        "Cancel this owned worker or discard its candidate. Does not terminate the Editor or user applications.", {},
        [&job](const nlohmann::json&) {
            job.Cancel();
            return EditorOperationResult{true, "CANCELLED", "Code task cancelled.", job.Status()};
        },
        EditorOperationKind::Session, EditorOperationPermission::ModifyProjectFiles);
    add(
        "project.code.apply",
        "Apply successfully compiled, reviewed source if the project and candidate are still unchanged. Module activation requires the normal human-controlled Rebuild and Reload boundary.",
        {},
        [&job, canApply = std::move(canApply)](const nlohmann::json&) {
            if (!canApply || !canApply())
                return EditorOperationResult{false, "PLAY_ACTIVE",
                                             "Stop Play and preview before applying project code."};
            std::string message;
            const bool ok = job.Apply(message);
            return EditorOperationResult{ok, ok ? "APPLIED" : "REJECTED", message, job.Status()};
        },
        EditorOperationKind::Mutation, EditorOperationPermission::ModifyProjectFiles);
}
} // namespace HIKARI::EDITOR
