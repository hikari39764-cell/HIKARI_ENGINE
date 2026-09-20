#include "HIKARI_EditorAuthoringSessionHost.h"

#include <utility>
#include <algorithm>
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI::EDITOR {
bool EditorAuthoringSessionHost::RegisterWorkspace(EditorAuthoringWorkspace workspace) {
    if (workspace.id.empty() || !workspace.create) return false;
    const auto id = workspace.id;
    return workspaces_.emplace(id, std::move(workspace)).second;
}

nlohmann::json EditorAuthoringSessionHost::Snapshot() const {
    nlohmann::json state{{"workspaces", nlohmann::json::array()}, {"sessions", nlohmann::json::array()}};
    for (const auto& [id, workspace] : workspaces_)
        state["workspaces"].push_back({{"id", id}, {"label", workspace.label}});
    for (const auto& [id, session] : sessions_) {
        auto item = session.backend->Describe();
        item["sessionId"] = id;
        item["workspace"] = session.workspace;
        item["recovered"] = session.recovered;
        item["checkpointed"] = session.checkpointed;
        state["sessions"].push_back(std::move(item));
    }
    state["recoveryErrors"] = recoveryErrors_;
    return state;
}

EditorOperationResult EditorAuthoringSessionHost::Open(const nlohmann::json& parameters) {
    const auto workspace = parameters.value("workspace", std::string{});
    const auto found = workspaces_.find(workspace);
    if (found == workspaces_.end())
        return {false, "UNKNOWN_WORKSPACE", "Query authoring.sessions for supported workspace providers."};
    if (sessions_.size() >= 8)
        return {false, "SESSION_LIMIT", "Close an unused authoring session before creating another."};
    auto backend = found->second.create();
    if (!backend) return {false, "SESSION_UNAVAILABLE", "The workspace cannot create a session in the current project."};
    auto result = backend->Open(parameters);
    if (!result.ok) return result;
    const auto id = "authoring-" + GenerateAssetGuid().value;
    result.data = backend->Describe();
    result.data["sessionId"] = id;
    if (result.data.contains("poll")) result.data["poll"]["sessionId"] = id;
    result.data["workspace"] = workspace;
    sessions_.emplace(id, Session{workspace, std::move(backend)});
    FlushRecovery();
    return result;
}

EditorOperationResult EditorAuthoringSessionHost::Execute(
    const std::string& action, const nlohmann::json& parameters) {
    const auto id = parameters.value("sessionId", std::string{});
    const auto found = sessions_.find(id);
    if (found == sessions_.end())
        return {false, "SESSION_NOT_FOUND", "The authoring session no longer exists. Open a session in the current project."};
    auto& backend = *found->second.backend;
    EditorOperationResult result;
    if (action == "authoring.preview" || action == "capture.preview" ||
        action == "vfx.preview.capture" || action == "material.verify") result = backend.Preview(parameters);
    else if (action == "authoring.capture_status") result = backend.PreviewStatus(parameters);
    else if (action == "authoring.reveal") {
        result = backend.Reveal();
        if (result.ok) {
            const auto previous = sessions_.find(VisibleSession(found->second.workspace));
            if (previous != sessions_.end() && previous != found) previous->second.backend->DetachVisible();
            visibleSessions_[found->second.workspace] = id;
        }
    }
    else if (action == "authoring.describe") {
        result = {true, "OK", "Authoring session state", backend.Describe()};
        result.data["operations"] = backend.Operations().RegisteredIds();
    } else if (action == "authoring.close") {
        // 草稿を黙って破棄しない。明示的な discard または保存が必要。
        const auto state = backend.Describe();
        if (state.value("dirty", false) && !parameters.value("discard", false))
            return {false, "UNSAVED_DOCUMENT", "Save the document or explicitly set discard=true to close its draft."};
        if (!RemoveRecovery(id))
            return {false, "RECOVERY_DELETE_FAILED", "Could not remove the recovery checkpoint; the document remains open."};
        if (VisibleSession(found->second.workspace) == id) {
            backend.DetachVisible();
            visibleSessions_.erase(found->second.workspace);
        }
        sessions_.erase(found);
        return {true, "OK", "Authoring session closed"};
    } else {
        auto targetParameters = parameters;
        targetParameters.erase("sessionId");
        result = backend.Operations().Execute(action, targetParameters);
    }
    if (!result.data.is_object()) result.data = nlohmann::json::object();
    FlushRecovery();
    result.data["sessionId"] = id;
    if (result.data.contains("poll")) result.data["poll"]["sessionId"] = id;
    return result;
}

void EditorAuthoringSessionHost::Tick(uint64_t frameIndex, float deltaSeconds) {
    for (auto& [id, session] : sessions_) session.backend->Tick(frameIndex, deltaSeconds);
    recoveryElapsed_ += (std::max)(0.0f, deltaSeconds);
    if (recoveryElapsed_ >= 2.0f) { recoveryElapsed_ = 0; FlushRecovery(); }
}

void EditorAuthoringSessionHost::Reset() {
    FlushRecovery();
    for (const auto& [workspace, id] : visibleSessions_) {
        const auto found = sessions_.find(id);
        if (found != sessions_.end()) found->second.backend->DetachVisible();
    }
    visibleSessions_.clear();
    sessions_.clear();
    recoveryRoot_.clear();
    recoveryErrors_ = nlohmann::json::array();
}

std::string EditorAuthoringSessionHost::VisibleSession(const std::string& workspace) const {
    const auto found = visibleSessions_.find(workspace);
    return found == visibleSessions_.end() ? std::string{} : found->second;
}
void EditorAuthoringSessionHost::LeaveDocument(const std::string& workspace, const std::string& requestedGuid) {
    const auto found = sessions_.find(VisibleSession(workspace));
    if (found != sessions_.end() && !requestedGuid.empty() &&
        found->second.backend->Describe().value("assetGuid",std::string{}) != requestedGuid) {
        found->second.backend->DetachVisible();
        visibleSessions_.erase(workspace);
    }
}

void EditorAuthoringSessionHost::CancelPreviews() {
    for (auto& [id, session] : sessions_) session.backend->CancelPreview();
}

bool EditorAuthoringSessionHost::HasUnsavedDocuments() const noexcept {
    try {
        for (const auto& [id, session] : sessions_)
            if (session.backend->Describe().value("dirty",false)) return true;
    } catch (...) { return true; }
    return false;
}

void EditorAuthoringSessionHost::RegisterOperations(EditorOperationRegistry& registry) {
    using T = EditorOperationParameterType;
    registry.SetTargetExecutor([this](const std::string& action, const nlohmann::json& parameters) {
        return Execute(action, parameters);
    });
    registry.SetTargetSelector([this, &registry](const std::string& action) {
        const auto* definition = registry.Find(action);
        return definition ? VisibleSession(definition->workspace) : std::string{};
    });
    registry.Register({"authoring.sessions", "List background workspaces and authoring sessions.", "general", {},
        [this](const nlohmann::json&) { return EditorOperationResult{true, "OK", "Authoring sessions", Snapshot()}; }});
    registry.Register({"authoring.session.open", "Create an independent background document session without opening UI.", "general",
        {{"workspace",T::String,true,"Workspace id from authoring.sessions"},
         {"assetGuid",T::String,false,"Existing source asset, omitted for a new document"},
         {"name",T::String,false,"New document name"}},
        [this](const nlohmann::json& p) { return Open(p); }});
    const auto registerSessionOperation=[&](const char* action,const char* summary,
        std::vector<EditorOperationParameter> parameters) {
        parameters.insert(parameters.begin(),{"sessionId",T::String,true,"Authoring session id"});
        registry.Register({action,summary,"general",std::move(parameters),
            [this,action=std::string(action)](const nlohmann::json& p) {return Execute(action,p);}});
    };
    registerSessionOperation("authoring.describe", "Inspect document state and available operations.", {});
    registerSessionOperation("authoring.preview", "Render a document preview without switching the workspace.",
        {{"time",T::Float,false,"Preview time in seconds"},{"width",T::Int,false,"Preview width"},{"height",T::Int,false,"Preview height"}});
    registerSessionOperation("authoring.capture_status", "Poll the document preview and return image attachments.", {});
    registerSessionOperation("authoring.reveal", "Open the live draft for manual editing, retaining its undo history.", {});
    registerSessionOperation("authoring.close", "Close a document session; dirty drafts require explicit discard.",
        {{"discard",T::Bool,false,"Explicitly discard an unsaved document"}});
}
}
