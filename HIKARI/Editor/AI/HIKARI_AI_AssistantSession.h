#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <json.hpp>

#include "Editor/AI/HIKARI_AI_ChatClient.h"

#include "Editor/AI/HIKARI_AI_Types.h"
#include "Editor/AI/HIKARI_AI_ConversationStore.h"
namespace HIKARI::EDITOR {
class AiAssistantPanel;
enum class EditorOperationPermission : uint8_t;
class AiAssistantSession {
public:
    using AiSceneActionCallback = std::function<AI_ASSISTANT::AiRequestResult(std::string_view, AiRequestSource)>;
    using AiCapabilityProvider = std::function<std::vector<AI_ASSISTANT::AiCapability>(AiRequestSource)>;
    using AiOperationPermissionResolver = std::function<std::optional<EditorOperationPermission>(std::string_view)>;

    explicit AiAssistantSession(AI_ASSISTANT::AiChatCompletion completion = AI_ASSISTANT::RunChatCompletionsStreaming);
    ~AiAssistantSession();

    void Stop();
    bool SubmitMessage(std::string text, std::vector<AI_ASSISTANT::AiImageAttachment> images = {});
    void Cancel();
    bool Resume();
    bool CanResume() const;
    bool IsBusy() const noexcept { return busy_.load(); }
    bool ConsumeCancellation() noexcept { return cancelOwnerRequested_.exchange(false); }
    AiProjectAccessMode ProjectAccessMode() const noexcept { return projectAccessMode_.load(); }
    AiProjectAccessMode RequestedProjectAccessMode() const noexcept;
    bool IsProjectAccessModeChangePending() const noexcept { return pendingProjectAccessMode_.has_value(); }
    bool RequestProjectAccessMode(AiProjectAccessMode mode);
    void SetSceneActionCallback(AiSceneActionCallback callback);
    void SetContextSnapshot(std::string snapshot);
    // Provider is evaluated on the submitting editor thread, never by the model worker.
    void SetContextProvider(std::function<std::string()> provider);
    void SetCapabilityProvider(AiCapabilityProvider callback, std::function<uint64_t()> revision = {});
    // Resolved on the editor thread before queued operations; a missing resolver fails closed.
    void SetOperationPermissionResolver(AiOperationPermissionResolver resolver);
    void RefreshCapabilities(bool onlyIfChanged = false);
    std::vector<AI_ASSISTANT::AiChatMessage> SnapshotMessages() const;
    AI_ASSISTANT::AiChatDelta SnapshotStreaming() const;
    struct TaskActivity {
        std::string phase = "planning", action, sessionId;
        std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
    };
    TaskActivity SnapshotActivity() const;
    void SetConfig(AI_ASSISTANT::AiChatConfig config);
    void SwitchProject(const std::filesystem::path& libraryRoot);
    bool NewConversation();
    bool SelectConversation(const std::string& id);
    std::vector<AI_ASSISTANT::AiConversationSummary> Conversations() const;
    void SetBridgeStatus(const std::string& status);
    void SetBridgeSessionToken(std::string token);
    AI_ASSISTANT::AiRequestResult ExecuteRequest(std::string_view json,
                                                 AiRequestSource source = AiRequestSource::BuiltInAi);
    std::vector<AI_ASSISTANT::AiCapability> Capabilities(AiRequestSource source = AiRequestSource::BuiltInAi) const;
    // Dispatches queued engine actions on the editor main thread. Called
    // once per frame from the scene controller, independent of the panel
    // window being visible.
    void PumpActions();
    // プロジェクトファイルの変更や外部プロセスの起動は、ユーザーが承認するまで保留する。
    struct ApprovalRequest {
        uint64_t id = 0;
        std::string action, permission, parameters;
    };
    std::vector<ApprovalRequest> PendingApprovals() const;
    bool HasPendingApprovals() const;
    void ResolveApproval(uint64_t id, bool approved);

private:
    friend class AiAssistantPanel;
    struct PendingAiAction {
        std::string json{};
        AiRequestSource source = AiRequestSource::BuiltInAi;
        uint64_t generation = 0;
        std::optional<AI_ASSISTANT::AiRequestResult> result{};
        uint64_t approvalId = 0;
        std::string permission{};
        bool approved = false;
    };

    AI_ASSISTANT::AiChatMessage ExecuteConversationAction(const std::string& actionJson);
    void ApplyPendingProjectAccessMode();
    void RunConversation(bool resume);
    bool StartConversation(std::string text, std::vector<AI_ASSISTANT::AiImageAttachment> images, bool resume);
    AI_ASSISTANT::AiRequestResult ExecuteRequestNow(std::string_view json, AiRequestSource source);
    void AppendMessage(const AI_ASSISTANT::AiChatMessage& message);
    void FlushConversation();
    void UpdateConversationStorage();
    void AppendProgress(const std::string& text, const std::string& reasoning, bool incomplete);
    void RecordUsage(const AI_ASSISTANT::AiChatUsage& usage, const AI_ASSISTANT::AiChatRequestStats& stats);
    void SetActivity(std::string phase, std::string action = {}, std::string sessionId = {});
    std::string BuildSystemPrompt() const;
    AI_ASSISTANT::AiChatConfig SnapshotConfig() const;
    std::string SnapshotContext() const;

    std::vector<AI_ASSISTANT::AiChatMessage> messages_{};
    AI_ASSISTANT::AiTaskCheckpoint taskCheckpoint_;
    mutable std::mutex messageMutex_{};
    TaskActivity activity_;
    mutable std::mutex streamMutex_{};
    mutable std::mutex stateMutex_{};
    mutable std::mutex actionMutex_{};
    std::condition_variable actionCv_{};
    std::vector<std::shared_ptr<PendingAiAction>> pendingActions_{};
    std::vector<std::shared_ptr<PendingAiAction>> awaitingApproval_{};
    uint64_t nextApprovalId_ = 1;
    std::thread::id mainThreadId_{};
    AI_ASSISTANT::AiChatDelta streamingPartial_{};
    bool streamingActive_ = false;
    std::string bridgeStatus_{};
    std::string bridgeSessionToken_{};
    AiSceneActionCallback sceneActionCallback_{};
    AiOperationPermissionResolver operationPermissionResolver_{};
    std::string contextSnapshot_{};
    std::function<std::string()> contextProvider_;
    std::string taskContext_{};
    AiCapabilityProvider capabilityProvider_{};
    std::function<uint64_t()> capabilityVersionProvider_;
    uint64_t capabilityRevision_ = ~uint64_t{0};
    std::array<std::vector<AI_ASSISTANT::AiCapability>, 2> capabilitySnapshots_{};
    std::atomic<AiProjectAccessMode> projectAccessMode_{AiProjectAccessMode::Content};
    std::optional<AiProjectAccessMode> pendingProjectAccessMode_{};
    AiProjectAccessMode conversationRequiredAccess_ = AiProjectAccessMode::Content;
    AI_ASSISTANT::AiChatConfig config_{};
    bool configLoaded_ = false;
    std::atomic<bool> busy_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> cancelOwnerRequested_{false};
    std::atomic<uint64_t> generation_{1};
    std::thread worker_{};
    AI_ASSISTANT::AiChatCompletion completion_;
    std::filesystem::path conversationFolder_, pendingConversationFolder_;
    bool projectSwitchPending_ = false;
    std::string conversationId_, storageError_;
    std::atomic<bool> conversationDirty_{false};
    uint64_t storageFrame_ = 0;
};

} // namespace HIKARI::EDITOR
