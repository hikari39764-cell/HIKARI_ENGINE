#pragma once
#include <filesystem>
#include "HIKARI_AI_ChatClient.h"
#include "HIKARI_AI_RequestContext.h"
#include "HIKARI_AI_Types.h"
namespace HIKARI::EDITOR::AI_ASSISTANT {
struct AiConversationSummary {
    std::string id, title;
};
// 表示ログとは別に、継続に必要なネイティブの呼び出しと応答を保持する。
struct AiTaskCheckpoint {
    bool active = false;
    std::string prompt, context;
    std::vector<AiChatMessage> messages;
    AiRequestContextState requestContext;
};
std::vector<AiConversationSummary> ListAiConversations(const std::filesystem::path& folder);
bool LoadAiConversation(const std::filesystem::path& folder, const std::string& id,
                        std::vector<AiChatMessage>& messages, std::string& error, AiTaskCheckpoint* task,
                        AiProjectAccessMode* requiredAccess);
bool SaveAiConversation(const std::filesystem::path& folder, const std::string& id,
                        const std::vector<AiChatMessage>& messages, std::string& error, const AiTaskCheckpoint* task,
                        AiProjectAccessMode requiredAccess);
} // namespace HIKARI::EDITOR::AI_ASSISTANT
