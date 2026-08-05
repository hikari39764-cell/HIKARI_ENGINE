#pragma once

#include <string>
#include <vector>

namespace HIKARI::EDITOR::AI_ASSISTANT {

    struct AiChatMessage {
        std::string role;
        std::string content;
    };

    struct AiChatConfig {
        std::string apiKey;
        std::string baseUrl;
        std::string model;
        bool valid = false;
    };

    bool LoadAiChatConfig(AiChatConfig& outConfig);
    bool SaveAiChatConfig(const AiChatConfig& config);
    bool ResetAiChatConfig();
    bool RunChatCompletions(
        const AiChatConfig& config,
        const std::vector<AiChatMessage>& messages,
        std::string& outContent,
        std::string& outError);
    std::string ExtractActionJson(const std::string& reply);

} // namespace HIKARI::EDITOR::AI_ASSISTANT
