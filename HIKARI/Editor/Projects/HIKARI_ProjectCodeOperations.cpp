#include "Editor/Projects/HIKARI_ProjectCodeJob.h"
#include "Editor/Projects/HIKARI_ProjectSdkDocs.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Project/HIKARI_ProjectContext.h"

#include <algorithm>

namespace HIKARI::EDITOR {
void RegisterProjectCodeOperations(EditorOperationRegistry& registry, ProjectCodeJob& job,
                                   std::function<EditorOperationResult()> reload) {
    using P = EditorOperationParameterType;
    auto add = [&](std::string id, std::string summary, std::vector<EditorOperationParameter> parameters,
                   std::function<EditorOperationResult(const nlohmann::json&)> action, EditorOperationKind kind,
                   EditorOperationPermission permission, bool approveEachTime = false) {
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
        definition.approveEachTime = approveEachTime;
        registry.Register(std::move(definition));
    };
    add(
        "project.sdk.index",
        "List the installed public SDK guides and headers with their intended starting points. This does not scan engine source.",
        {{"keyword", P::String, false, "Optional case-insensitive header path filter."},
         {"offset", P::Int, false, "Header page offset from a previous result.", {{"minimum", 0}}},
         {"limit", P::Int, false, "Header page size from 1 to 64; defaults to 32.", {{"minimum", 1}, {"maximum", 64}}}},
        [](const nlohmann::json& p) {
            const size_t offset = static_cast<size_t>((std::max)(0, p.value("offset", 0)));
            const size_t limit = static_cast<size_t>(std::clamp(p.value("limit", 32), 1, 64));
            auto index =
                PROJECT_SDK::Index(ProjectCodeJob::SdkRoot(), p.value("keyword", std::string{}), offset, limit);
            const bool complete = index.value("complete", false);
            return EditorOperationResult{complete, complete ? "OK" : "SDK_INCOMPLETE",
                                         complete ? "Installed public SDK index."
                                                  : "Installed SDK documentation or headers are incomplete.",
                                         std::move(index)};
        },
        EditorOperationKind::Query, EditorOperationPermission::ReadEditorState);
    add(
        "project.sdk.read",
        "Read one bounded excerpt from an installed public SDK header or guide. Paths stay inside Sdk/include/HIKARI or Sdk/docs.",
        {{"path", P::String, true,
          "Path from project.sdk.index, such as include/HIKARI/Project/WorldView.h or docs/game-project/MakingAGame.md."},
         {"startLine", P::Int, false, "One-based first line; defaults to 1.", {{"minimum", 1}}},
         {"maxLines",
          P::Int,
          false,
          "Lines to return, 1 to 400; defaults to 160.",
          {{"minimum", 1}, {"maximum", 400}}}},
        [](const nlohmann::json& p) {
            const size_t startLine = static_cast<size_t>((std::max)(1, p.value("startLine", 1)));
            const size_t maxLines = static_cast<size_t>(std::clamp(p.value("maxLines", 160), 1, 400));
            return EditorOperationResult{
                true, "OK", "Installed public SDK file.",
                PROJECT_SDK::ReadFile(ProjectCodeJob::SdkRoot(), p.at("path").get<std::string>(), startLine, maxLines)};
        },
        EditorOperationKind::Query, EditorOperationPermission::ReadEditorState);
    add(
        "project.build",
        "Build a copy of the current project source (Source, Tests, Tools, cmake) in a hidden worker and run the optional "
        "Tests/HikariVerification.json behavior checks. The source and the loaded module stay unchanged. Poll "
        "project.build.status; compiling does not prove visual or gameplay correctness.",
        {},
        [&job](const nlohmann::json&) {
            std::string message;
            const bool ok = job.Start(message);
            nlohmann::json data = job.Status();
            if (ok) {
                data["pending"] = true;
                data["poll"] = {{"operation", "project.build.status"}, {"parameters", nlohmann::json::object()}};
            }
            return EditorOperationResult{ok, ok ? "STARTED" : "REJECTED", message, std::move(data)};
        },
        EditorOperationKind::Session, EditorOperationPermission::ExecuteExternalProcess);
    add(
        "project.build.status", "Report the current project build: build and behavior results and the worker message.",
        {},
        [&job](const nlohmann::json&) {
            job.Poll();
            nlohmann::json data = job.Status();
            if (job.Running()) {
                data["pending"] = true;
                data["poll"] = {{"operation", "project.build.status"}, {"parameters", nlohmann::json::object()}};
            }
            return EditorOperationResult{true, "OK", "Project build status.", std::move(data)};
        },
        EditorOperationKind::Query, EditorOperationPermission::ReadEditorState);
    add(
        "project.build.cancel", "Cancel the running project build. Does not terminate the Editor or user applications.",
        {},
        [&job](const nlohmann::json&) {
            job.Cancel();
            return EditorOperationResult{true, "CANCELLED", "Project build cancelled.", job.Status()};
        },
        EditorOperationKind::Session, EditorOperationPermission::ModifyEditorState);
    add(
        "project.reload",
        "Rebuild the project and restart the Editor to load the new module (Project > Rebuild and Reload). Requires "
        "saved documents and no Play. Asks the user every time; reconnect after the Editor restarts.",
        {}, [reload = std::move(reload)](const nlohmann::json&) { return reload(); }, EditorOperationKind::Session,
        EditorOperationPermission::ExecuteExternalProcess, true);
}
} // namespace HIKARI::EDITOR
