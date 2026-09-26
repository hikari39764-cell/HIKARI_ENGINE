#include "HIKARI_EditorAuthoringSessionHost.h"

#include <utility>
#include <algorithm>
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI::EDITOR {
bool EditorAuthoringSessionHost::RegisterWorkspace(EditorAuthoringWorkspace workspace) {
    if (workspace.id.empty() || !workspace.create)
        return false;
    const auto id = workspace.id;
    return workspaces_.emplace(id, std::move(workspace)).second;
}

nlohmann::json EditorAuthoringSessionHost::DescribeSession(const std::string& id, const Session& session) const {
    auto item = session.backend->Describe();
    item["sessionId"] = id;
    item["workspace"] = session.workspace;
    if (item.value("name", std::string{}).empty())
        item["name"] = session.name.empty() ? session.workspace : session.name;
    item["visible"] = VisibleSession(session.workspace) == id;
    item["touched"] = session.touched;
    item["recovered"] = session.recovered;
    item["checkpointed"] = session.checkpointed;
    return item;
}
nlohmann::json EditorAuthoringSessionHost::Snapshot(bool compact) const {
    nlohmann::json state{{"workspaces", nlohmann::json::array()}, {"sessions", nlohmann::json::array()}};
    for (const auto& [id, workspace] : workspaces_)
        state["workspaces"].push_back({{"id", id}, {"label", workspace.label}});
    std::string latestSession;
    if (compact && !sessions_.empty()) {
        const auto latest =
            std::max_element(sessions_.begin(), sessions_.end(), [](const auto& left, const auto& right) {
                return left.second.touched < right.second.touched;
            });
        latestSession = latest->first;
    }
    for (const auto& [id, session] : sessions_) {
        if (!compact || id == latestSession) {
            state["sessions"].push_back(DescribeSession(id, session));
            continue;
        }
        const auto workspace = workspaces_.find(session.workspace);
        const std::string fallbackLabel = workspace == workspaces_.end() ? session.workspace : workspace->second.label;
        state["sessions"].push_back({{"sessionId", id},
                                     {"workspace", session.workspace},
                                     {"name", session.name.empty() ? fallbackLabel : session.name}});
    }
    state["recoveryErrors"] = recoveryErrors_;
    return state;
}

EditorOperationResult EditorAuthoringSessionHost::Open(const nlohmann::json& parameters) {
    const auto workspace = parameters.value("workspace", std::string{});
    const auto found = workspaces_.find(workspace);
    if (found == workspaces_.end())
        return {false, "UNKNOWN_WORKSPACE", "Query authoring.sessions for supported workspace providers."};
    const auto guid = parameters.value("assetGuid", std::string{});
    if (!guid.empty())
        for (auto& [id, session] : sessions_) {
            if (session.workspace == workspace &&
                session.backend->Describe().value("assetGuid", std::string{}) == guid) {
                session.touched = ++touchSerial_;
                auto result = Execute("authoring.describe", {{"sessionId", id}});
                result.data["reused"] = true;
                return result;
            }
        }
    auto backend = found->second.create();
    if (!backend)
        return {false, "SESSION_UNAVAILABLE", "The workspace cannot create a session in the current project."};
    auto result = backend->Open(parameters);
    if (!result.ok)
        return result;
    const auto id = "authoring-" + GenerateAssetGuid().value;
    result.data = backend->Describe();
    result.data["sessionId"] = id;
    if (result.data.contains("poll"))
        result.data["poll"]["sessionId"] = id;
    result.data["workspace"] = workspace;
    auto [inserted, unused] = sessions_.emplace(id, Session{workspace, std::move(backend)});
    inserted->second.touched = ++touchSerial_;
    inserted->second.name = parameters.value("name", std::string{});
    FlushRecovery();
    return result;
}

EditorOperationResult EditorAuthoringSessionHost::Execute(const std::string& action, const nlohmann::json& parameters) {
    const auto id = parameters.value("sessionId", std::string{});
    const auto found = sessions_.find(id);
    if (found == sessions_.end())
        return {false, "SESSION_NOT_FOUND",
                "The authoring session no longer exists. Open a session in the current project."};
    auto& backend = *found->second.backend;
    EditorOperationResult result;
    if (action == "authoring.preview")
        result = backend.Preview(parameters);
    else if (action == "authoring.cancel_preview") {
        backend.CancelPreview();
        result = {true, "OK", "Preview cancelled; document retained."};
    } else if (action == "authoring.capture_status")
        result = backend.PreviewStatus(parameters);
    else if (action == "authoring.reveal") {
        result = backend.Reveal();
        if (result.ok) {
            const auto previous = sessions_.find(VisibleSession(found->second.workspace));
            if (previous != sessions_.end() && previous != found)
                previous->second.backend->DetachVisible();
            visibleSessions_[found->second.workspace] = id;
            foregroundChosen_.erase(found->second.workspace);
        }
    } else if (action == "authoring.describe") {
        result = {true, "OK", "Authoring session state", backend.Describe()};
        result.data["operations"] = backend.Operations().RegisteredIds();
    } else if (action == "authoring.close") {
        // 草稿を黙って破棄しない。明示的な discard または保存が必要。
        const auto state = backend.Describe();
        if (state.value("dirty", false) && !parameters.value("discard", false))
            return {false, "UNSAVED_DOCUMENT", "Save the document or explicitly set discard=true to close its draft."};
        if (!RemoveRecovery(id))
            return {false, "RECOVERY_DELETE_FAILED",
                    "Could not remove the recovery checkpoint; the document remains open."};
        if (VisibleSession(found->second.workspace) == id) {
            backend.DetachVisible();
            visibleSessions_.erase(found->second.workspace);
        }
        sessions_.erase(found);
        return {true, "OK", "Authoring session closed"};
    } else {
        auto targetParameters = parameters;
        targetParameters.erase("sessionId");
        if (!backend.Operations().IsRegistered(action)) {
            nlohmann::json candidates = nlohmann::json::array();
            for (const auto& [candidateId, candidate] : sessions_)
                if (candidate.backend->Operations().IsRegistered(action))
                    candidates.push_back({{"sessionId", candidateId}, {"workspace", candidate.workspace}});
            return {false,
                    "OPERATION_TARGET_MISMATCH",
                    "The operation is not available in this document session.",
                    {{"sessionId", id},
                     {"workspace", found->second.workspace},
                     {"compatibleSessions", std::move(candidates)},
                     {"availableOperations", backend.Operations().RegisteredIds()}}};
        }
        if (backend.Operations().Find(action)->requiresCleanDocument && backend.Describe().value("dirty", false))
            return {false,
                    "UNSAVED_DOCUMENT",
                    "Save the current document or open a separate session before replacing it. No dialog was opened.",
                    {{"sessionId", id}, {"state", backend.Describe()}}};
        result = backend.Operations().Execute(action, targetParameters);
    }
    if (!result.data.is_object())
        result.data = nlohmann::json::object();
    if (result.ok && action != "authoring.describe" && action != "authoring.capture_status")
        found->second.touched = ++touchSerial_;
    FlushRecovery();
    result.data["sessionId"] = id;
    if (result.data.contains("poll"))
        result.data["poll"]["sessionId"] = id;
    return result;
}

void EditorAuthoringSessionHost::Tick(uint64_t frameIndex, float deltaSeconds) {
    for (auto& [id, session] : sessions_)
        session.backend->Tick(frameIndex, deltaSeconds);
    recoveryElapsed_ += (std::max)(0.0f, deltaSeconds);
    if (recoveryElapsed_ >= 2.0f) {
        recoveryElapsed_ = 0;
        FlushRecovery();
    }
}

void EditorAuthoringSessionHost::Reset() {
    FlushRecovery();
    for (const auto& [workspace, id] : visibleSessions_) {
        const auto found = sessions_.find(id);
        if (found != sessions_.end())
            found->second.backend->DetachVisible();
    }
    visibleSessions_.clear();
    foregroundChosen_.clear();
    touchSerial_ = 0;
    sessions_.clear();
    recoveryRoot_.clear();
    recoveryErrors_ = nlohmann::json::array();
}

std::string EditorAuthoringSessionHost::VisibleSession(const std::string& workspace) const {
    const auto found = visibleSessions_.find(workspace);
    return found == visibleSessions_.end() ? std::string{} : found->second;
}
void EditorAuthoringSessionHost::ShowForeground(const std::string& workspace) {
    const auto found = sessions_.find(VisibleSession(workspace));
    if (found != sessions_.end())
        found->second.backend->DetachVisible();
    visibleSessions_.erase(workspace);
    foregroundChosen_[workspace] = true;
}
nlohmann::json EditorAuthoringSessionHost::WorkspaceDocuments(const std::string& workspace) const {
    auto result = nlohmann::json::array();
    for (const auto& [id, session] : sessions_)
        if (session.workspace == workspace)
            result.push_back(DescribeSession(id, session));
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.value("touched", uint64_t{0}) > b.value("touched", uint64_t{0});
    });
    return result;
}
bool EditorAuthoringSessionHost::ActivateDocument(const std::string& workspace, const std::string& requestedGuid,
                                                  bool foregroundHasDocument) {
    auto documents = WorkspaceDocuments(workspace);
    std::string selected;
    if (!requestedGuid.empty()) {
        for (const auto& item : documents)
            if (item.value("assetGuid", std::string{}) == requestedGuid) {
                selected = item["sessionId"];
                break;
            }
        if (selected.empty()) {
            ShowForeground(workspace);
            return false;
        }
    } else {
        selected = VisibleSession(workspace);
        if (documents.empty())
            return false;
        if ((followDocuments_ && !foregroundChosen_[workspace]) ||
            (selected.empty() && !foregroundHasDocument && !foregroundChosen_[workspace]))
            selected = documents.front()["sessionId"];
    }
    if (selected.empty())
        return false;
    if (selected != VisibleSession(workspace)) {
        const auto previous = sessions_.find(VisibleSession(workspace));
        if (previous != sessions_.end())
            previous->second.backend->DetachVisible();
        visibleSessions_[workspace] = selected;
        foregroundChosen_.erase(workspace);
    }
    return true;
}

void EditorAuthoringSessionHost::CancelPreviews() {
    for (auto& [id, session] : sessions_)
        session.backend->CancelPreview();
}

bool EditorAuthoringSessionHost::HasUnsavedDocuments() const noexcept {
    try {
        for (const auto& [id, session] : sessions_)
            if (session.backend->Describe().value("dirty", false))
                return true;
    } catch (...) {
        return true;
    }
    return false;
}

void EditorAuthoringSessionHost::RegisterOperations(EditorOperationRegistry& registry) {
    using T = EditorOperationParameterType;
    registry.SetTargetExecutor(
        [this](const std::string& action, const nlohmann::json& parameters) { return Execute(action, parameters); });
    registry.SetTargetSelector([this, &registry](const std::string& action) {
        const auto* definition = registry.Find(action);
        return definition ? VisibleSession(definition->workspace) : std::string{};
    });
    registry.Register(
        {"authoring.sessions",
         "List background workspaces and authoring sessions.",
         "general",
         {},
         [this](const nlohmann::json&) { return EditorOperationResult{true, "OK", "Authoring sessions", Snapshot()}; }},
        HIKARI::EDITOR::EditorOperationKind::Query, false);
    EditorOperationDefinition open{
        "authoring.session.open",
        "Open or reuse a live document session without switching the user workspace.",
        "general",
        {{"workspace", T::String, true, "Workspace id from authoring.sessions"},
         {"assetGuid", T::String, false, "Existing source asset, omitted for a new document"},
         {"name", T::String, false, "New document name"}},
        [this](const nlohmann::json& p) { return Open(p); }};
    open.entryPoint = true;
    registry.Register(std::move(open), EditorOperationKind::Session, false);
    const auto registerSessionOperation = [&](const char* action, const char* summary,
                                              std::vector<EditorOperationParameter> parameters,
                                              EditorOperationKind kind = EditorOperationKind::Session) {
        parameters.insert(parameters.begin(), {"sessionId", T::String, true, "Authoring session id"});
        registry.Register(
            {action, summary, "general", std::move(parameters),
             [this, action = std::string(action)](const nlohmann::json& p) { return Execute(action, p); }},
            kind, false);
    };
    registerSessionOperation("authoring.describe", "Inspect document state and available operations.", {},
                             EditorOperationKind::Query);
    registerSessionOperation("authoring.preview", "Render a document preview without switching the workspace.",
                             {{"time", T::Float, false, "Preview time in seconds"},
                              {"width", T::Int, false, "Preview width"},
                              {"height", T::Int, false, "Preview height"}});
    registerSessionOperation("authoring.cancel_preview", "Cancel a preview request without changing the document.", {});
    registerSessionOperation("authoring.capture_status", "Poll the document preview and return image attachments.", {},
                             EditorOperationKind::Query);
    registerSessionOperation("authoring.reveal", "Open the live draft for manual editing, retaining its undo history.",
                             {});
    registerSessionOperation("authoring.close", "Close a document session; dirty drafts require explicit discard.",
                             {{"discard", T::Bool, false, "Explicitly discard an unsaved document"}});
}
} // namespace HIKARI::EDITOR
