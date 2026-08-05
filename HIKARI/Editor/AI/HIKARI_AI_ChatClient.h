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
    std::string GetAiUserConfigPath();
    std::string GetAiVisionSkillEnvPath();
    std::string AiJsonEscape(const std::string& value);
    bool RunApiRequest(
        const AiChatConfig& config,
        const std::string& requestBody,
        std::string& outContent,
        std::string& outError);
    std::string ExtractJsonField(
        std::string_view json,
        std::string_view field);
    std::vector<std::string> ExtractAllActionJson(
        const std::string& reply);
    bool RunChatCompletions(
        const AiChatConfig& config,
        const std::vector<AiChatMessage>& messages,
        std::string& outContent,
        std::string& outError);
    std::string ExtractActionJson(const std::string& reply);

} // namespace HIKARI::EDITOR::AI_ASSISTANT
