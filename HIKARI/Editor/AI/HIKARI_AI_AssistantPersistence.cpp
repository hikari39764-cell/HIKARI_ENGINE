#include "HIKARI_AI_AssistantSession.h"
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI::EDITOR {
void AiAssistantSession::FlushConversation() {
    if (!conversationDirty_.exchange(false) || conversationFolder_.empty() || conversationId_.empty())
        return;
    std::vector<AI_ASSISTANT::AiChatMessage> messages;
    AI_ASSISTANT::AiTaskCheckpoint checkpoint;
    AiProjectAccessMode requiredAccess = AiProjectAccessMode::DevelopProject;
    {
        std::lock_guard<std::mutex> lock(messageMutex_);
        messages = messages_;
        checkpoint = taskCheckpoint_;
        requiredAccess = conversationRequiredAccess_;
    }
    if (!AI_ASSISTANT::SaveAiConversation(conversationFolder_, conversationId_, messages, storageError_, &checkpoint,
                                          requiredAccess))
        conversationDirty_.store(true);
}
void AiAssistantSession::SwitchProject(const std::filesystem::path& libraryRoot) {
    const auto folder = libraryRoot / "EditorAI" / "Conversations";
    if (folder == conversationFolder_ && !projectSwitchPending_)
        return;
    Cancel();
    pendingConversationFolder_ = folder;
    projectSwitchPending_ = true;
    pendingProjectAccessMode_.reset();
    projectAccessMode_.store(AiProjectAccessMode::Content);
    RefreshCapabilities(false);
    UpdateConversationStorage();
}
void AiAssistantSession::UpdateConversationStorage() {
    if (projectSwitchPending_ && !IsBusy()) {
        FlushConversation();
        conversationFolder_ = pendingConversationFolder_;
        projectSwitchPending_ = false;
        conversationId_.clear();
        {
            std::lock_guard<std::mutex> lock(messageMutex_);
            messages_.clear();
            taskCheckpoint_ = {};
            conversationRequiredAccess_ = AiProjectAccessMode::Content;
        }
        stopRequested_.store(false);
        const auto saved = Conversations();
        if (saved.empty() || !SelectConversation(saved.front().id))
            NewConversation();
    }
    if (!IsBusy() && !projectSwitchPending_ && !pendingProjectAccessMode_)
        stopRequested_.store(false);
    if (++storageFrame_ % 120 == 0 || (!IsBusy() && conversationDirty_.load()))
        FlushConversation();
}
bool AiAssistantSession::NewConversation() {
    if (IsBusy() || projectSwitchPending_ || pendingProjectAccessMode_)
        return false;
    FlushConversation();
    conversationId_ = GenerateAssetGuid().value;
    {
        std::lock_guard<std::mutex> lock(messageMutex_);
        messages_.clear();
        taskCheckpoint_ = {};
        conversationRequiredAccess_ = projectAccessMode_.load();
    }
    storageError_.clear();
    return true;
}
bool AiAssistantSession::SelectConversation(const std::string& id) {
    if (IsBusy() || projectSwitchPending_)
        return false;
    std::vector<AI_ASSISTANT::AiChatMessage> loaded;
    AI_ASSISTANT::AiTaskCheckpoint checkpoint;
    AiProjectAccessMode requiredAccess = AiProjectAccessMode::DevelopProject;
    if (!AI_ASSISTANT::LoadAiConversation(conversationFolder_, id, loaded, storageError_, &checkpoint, &requiredAccess))
        return false;
    const auto currentAccess = projectAccessMode_.load();
    if (static_cast<uint8_t>(currentAccess) < static_cast<uint8_t>(requiredAccess)) {
        storageError_ = "此历史对话需要 " + std::string(TEXT::EnumToName(kAiProjectAccessModeNames, requiredAccess)) +
                        " 权限。当前模式不会自动提升；请手动切换模式后重新选择，或开始新对话。";
        return false;
    }
    FlushConversation();
    conversationId_ = id;
    {
        std::lock_guard<std::mutex> lock(messageMutex_);
        messages_ = std::move(loaded);
        taskCheckpoint_ = std::move(checkpoint);
        conversationRequiredAccess_ = requiredAccess;
    }
    return true;
}
std::vector<AI_ASSISTANT::AiConversationSummary> AiAssistantSession::Conversations() const {
    return AI_ASSISTANT::ListAiConversations(conversationFolder_);
}
} // namespace HIKARI::EDITOR
