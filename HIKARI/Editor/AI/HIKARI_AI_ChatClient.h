#pragma once

#include <functional>
#include <cstdint>
#include <string>
#include <vector>
#include <json.hpp>

namespace HIKARI::EDITOR::AI_ASSISTANT {

    struct AiImageAttachment {
        std::string path;
        std::string label;
        std::string detail = "auto";
    };

    struct AiChatMessage {
        std::string role;
        std::string content;
        std::vector<AiImageAttachment> images{};
        nlohmann::json data = nlohmann::json::object();
    };

    struct AiChatConfig {
        std::string apiKey;
        std::string baseUrl;
        std::string model;
        bool valid = false;
        bool imageInputEnabled = true;
        int maxToolRounds = 40;
        int toolHistoryKiB = 24;
        bool requestUsage = true;
        std::function<bool()> shouldCancel{};
    };

    struct AiChatUsage {
        // -1 means the provider did not report this field, never zero usage.
        int64_t inputTokens = -1, outputTokens = -1, cachedTokens = -1;
    };
    struct AiChatRequestStats {
        size_t sourceTextBytes = 0, sentTextBytes = 0, sentImages = 0;
    };

    bool ReadAiImageDataUri(const std::string& path, std::string& uri,
        std::string& error);
    bool BuildAiChatRequest(const AiChatConfig& config,
        const std::vector<AiChatMessage>& messages, bool stream,
        std::string& body, std::string& error, AiChatRequestStats* stats = nullptr);

    using AiChatStreamCallback =
        std::function<bool(const std::string& delta)>;

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
        std::string& outError, AiChatUsage* usage = nullptr);
    std::string ExtractJsonField(
        std::string_view json,
        std::string_view field);
    std::string ExtractJsonNumberField(
        std::string_view json,
        std::string_view field);
    std::vector<std::string> ExtractAllActionJson(
        const std::string& reply);
    bool RunChatCompletionsStreaming(
        const AiChatConfig& config,
        const std::vector<AiChatMessage>& messages,
        const AiChatStreamCallback& onDelta,
        std::string& outFull,
        std::string& outError, AiChatUsage* usage = nullptr,
        AiChatRequestStats* stats = nullptr);

} // namespace HIKARI::EDITOR::AI_ASSISTANT
