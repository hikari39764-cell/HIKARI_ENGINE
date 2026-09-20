#include "HIKARI_AI_AssistantSession.h"
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI::EDITOR {
void AiAssistantSession::FlushConversation() {
    if (!conversationDirty_.exchange(false) || conversationFolder_.empty() || conversationId_.empty()) return;
    std::vector<AI_ASSISTANT::AiChatMessage> messages;
    { std::lock_guard<std::mutex> lock(messageMutex_); messages=messages_; }
    if (!AI_ASSISTANT::SaveAiConversation(conversationFolder_,conversationId_,messages,storageError_))
        conversationDirty_.store(true);
}
void AiAssistantSession::SwitchProject(const std::filesystem::path& libraryRoot) {
    const auto folder=libraryRoot/"EditorAI"/"Conversations";
    if (folder==conversationFolder_ && !projectSwitchPending_) return;
    Cancel();
    pendingConversationFolder_=folder;
    projectSwitchPending_=true;
    UpdateConversationStorage();
}
void AiAssistantSession::UpdateConversationStorage() {
    if (projectSwitchPending_ && !IsBusy()) {
        FlushConversation();
        conversationFolder_=pendingConversationFolder_;
        projectSwitchPending_=false;
        conversationId_.clear();
        { std::lock_guard<std::mutex> lock(messageMutex_); messages_.clear(); }
        stopRequested_.store(false);
        const auto saved=Conversations();
        if (saved.empty() || !SelectConversation(saved.front().id)) NewConversation();
    }
    if (!IsBusy() && !projectSwitchPending_) stopRequested_.store(false);
    if (++storageFrame_%120==0 || (!IsBusy() && conversationDirty_.load())) FlushConversation();
}
bool AiAssistantSession::NewConversation() {
    if (IsBusy() || projectSwitchPending_) return false;
    FlushConversation();
    conversationId_=GenerateAssetGuid().value;
    { std::lock_guard<std::mutex> lock(messageMutex_); messages_.clear(); }
    storageError_.clear();
    return true;
}
bool AiAssistantSession::SelectConversation(const std::string& id) {
    if (IsBusy() || projectSwitchPending_) return false;
    std::vector<AI_ASSISTANT::AiChatMessage> loaded;
    if (!AI_ASSISTANT::LoadAiConversation(conversationFolder_,id,loaded,storageError_)) return false;
    FlushConversation();
    conversationId_=id;
    { std::lock_guard<std::mutex> lock(messageMutex_); messages_=std::move(loaded); }
    return true;
}
std::vector<AI_ASSISTANT::AiConversationSummary> AiAssistantSession::Conversations() const {
    return AI_ASSISTANT::ListAiConversations(conversationFolder_);
}
}
