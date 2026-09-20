#pragma once
#include <filesystem>
#include "HIKARI_AI_ChatClient.h"
namespace HIKARI::EDITOR::AI_ASSISTANT {
struct AiConversationSummary { std::string id, title; };
std::vector<AiConversationSummary> ListAiConversations(const std::filesystem::path& folder);
bool LoadAiConversation(const std::filesystem::path& folder, const std::string& id,
    std::vector<AiChatMessage>& messages, std::string& error);
bool SaveAiConversation(const std::filesystem::path& folder, const std::string& id,
    const std::vector<AiChatMessage>& messages, std::string& error);
}
