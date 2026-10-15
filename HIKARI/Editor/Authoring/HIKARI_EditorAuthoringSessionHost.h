#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <any>
#include <filesystem>
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"

namespace HIKARI::EDITOR {

// ワークスペースの文書・操作を UI の可視性から切り離す。各領域は既存の
// コマンドと履歴を提供し、ホストは意味や別の Undo スタックを持たない。
class EditorAuthoringSession {
public:
    virtual ~EditorAuthoringSession() = default;
    virtual EditorOperationRegistry& Operations() = 0;
    virtual EditorOperationResult Open(const nlohmann::json& parameters) = 0;
    virtual nlohmann::json Describe() const = 0;
    virtual std::any ControllerAddress() = 0;
    virtual void Tick(uint64_t, float) {}
    virtual bool HasInteractiveEdit() const { return false; }
    virtual EditorOperationResult Preview(const nlohmann::json&) {
        return {false, "NO_VISUAL_PREVIEW",
                "This workspace provides structured validation instead of a rendered preview."};
    }
    virtual EditorOperationResult Reveal() = 0;
    virtual EditorOperationResult PreviewStatus(const nlohmann::json&) {
        return {false, "NO_CAPTURE", "No capture is pending."};
    }
    virtual void CancelPreview() {}
    // 表示先から外す前に、文書を借用している描画要求を解除する。
    virtual void DetachVisible() {}
    virtual nlohmann::json Checkpoint() const { return nullptr; }
    virtual EditorOperationResult Restore(const nlohmann::json&) {
        return {false, "RECOVERY_UNSUPPORTED", "This workspace cannot restore a background draft yet."};
    }
};

struct EditorAuthoringWorkspace {
    std::string id;
    std::string label;
    std::function<std::unique_ptr<EditorAuthoringSession>()> create;
};

class EditorAuthoringSessionHost {
public:
    bool RegisterWorkspace(EditorAuthoringWorkspace workspace);
    void RegisterOperations(EditorOperationRegistry& registry);
    EditorOperationResult Execute(const std::string& action, const nlohmann::json& parameters);
    void Tick(uint64_t frameIndex, float deltaSeconds);
    void Reset();
    void SetRecoveryRoot(const std::filesystem::path& libraryRoot);
    void FlushRecovery();
    bool HasUnsavedDocuments() const noexcept;
    bool HasInteractiveEdit(const std::string& id) const {
        const auto found = sessions_.find(id);
        return found != sessions_.end() && found->second.backend->HasInteractiveEdit();
    }
    nlohmann::json Snapshot(bool compact = false) const;
    std::string VisibleSession(const std::string& workspace) const;
    bool ActivateDocument(const std::string& workspace, const std::string& requestedGuid, bool foregroundHasDocument);
    void ShowForeground(const std::string& workspace);
    bool FollowDocuments() const { return followDocuments_; }
    void SetFollowDocuments(bool enabled) {
        followDocuments_ = enabled;
        if (enabled)
            foregroundChosen_.clear();
    }
    nlohmann::json WorkspaceDocuments(const std::string& workspace) const;
    template <class T>
    T& ResolveController(const std::string& workspace, T& fallback) const {
        const auto found = sessions_.find(VisibleSession(workspace));
        if (found == sessions_.end())
            return fallback;
        const auto address = found->second.backend->ControllerAddress();
        const auto typed = std::any_cast<T*>(&address);
        return typed ? **typed : fallback;
    }

private:
    struct Session {
        std::string workspace;
        std::unique_ptr<EditorAuthoringSession> backend;
        bool recovered = false;
        bool checkpointed = false;
        std::string checkpoint;
        std::string checkpointState;
        uint64_t touched = 0;
        std::string name;
    };
    bool RemoveRecovery(const std::string& id);
    nlohmann::json DescribeSession(const std::string& id, const Session& session) const;
    EditorOperationResult Open(const nlohmann::json& parameters);
    std::unordered_map<std::string, EditorAuthoringWorkspace> workspaces_;
    std::unordered_map<std::string, Session> sessions_;
    std::unordered_map<std::string, std::string> visibleSessions_;
    std::filesystem::path recoveryRoot_;
    nlohmann::json recoveryErrors_ = nlohmann::json::array();
    float recoveryElapsed_ = 0;
    uint64_t touchSerial_ = 0;
    bool followDocuments_ = false;
    std::unordered_map<std::string, bool> foregroundChosen_;
};
} // namespace HIKARI::EDITOR
