#pragma once

#include <functional>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "Core/Text/HIKARI_EnumNames.h"
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

inline constexpr int kDefaultAiToolRounds = 128;
inline constexpr int kMaxAiToolRounds = 4096;
struct AiToolReply;
struct AiRequestContextState;
enum class AiReasoningContentPolicy : uint8_t { Preserve, Omit };
inline constexpr TEXT::EnumName<AiReasoningContentPolicy> kAiReasoningContentPolicyNames[]{
    {AiReasoningContentPolicy::Preserve, "preserve"}, {AiReasoningContentPolicy::Omit, "omit"}};
struct AiChatConfig {
    std::string apiKey;
    std::string baseUrl;
    std::string model;
    bool valid = false;
    bool imageInputEnabled = true;
    int maxToolRounds = kDefaultAiToolRounds;
    int maxTaskTokens = 0;        // Reported input + output per run; zero disables the soft limit.
    int maxNoProgressRounds = 12; // Zero disables automatic pausing; diagnostics still appear.
    int toolHistoryKiB = 24;
    AiRequestContextState* requestContext = nullptr; // Borrowed by one active conversation only.
    size_t requestContextPrefixMessages = 0;         // Dynamic system/context prefix before task messages.
    AiReasoningContentPolicy reasoningContentPolicy = AiReasoningContentPolicy::Preserve;
    bool showReasoning = true;
    bool requestUsage = true;
    bool editorTools = false;
    std::function<bool()> shouldCancel{};
};

struct AiChatUsage {
    // -1 means the provider did not report this field, never zero usage.
    int64_t inputTokens = -1, outputTokens = -1, cachedTokens = -1;
};
struct AiChatRequestStats {
    size_t sourceTextBytes = 0, sentTextBytes = 0, sentImages = 0;
};

bool ReadAiImageDataUri(const std::string& path, std::string& uri, std::string& error);
bool BuildAiChatRequest(const AiChatConfig& config, const std::vector<AiChatMessage>& messages, bool stream,
                        std::string& body, std::string& error, AiChatRequestStats* stats = nullptr);

struct AiChatDelta {
    std::string content, reasoning;
};
using AiChatStreamCallback = std::function<bool(const AiChatDelta& delta)>;

bool LoadAiChatConfig(AiChatConfig& outConfig);
bool SaveAiChatConfig(const AiChatConfig& config);
bool ResetAiChatConfig();
std::string GetAiUserConfigPath();
bool RunApiRequest(const AiChatConfig& config, const std::string& requestBody, std::string& outContent,
                   std::string& outError, AiChatUsage* usage = nullptr);
std::string ExtractJsonField(std::string_view json, std::string_view field);
std::string ExtractJsonNumberField(std::string_view json, std::string_view field);
bool RunChatCompletionsStreaming(const AiChatConfig& config, const std::vector<AiChatMessage>& messages,
                                 const AiChatStreamCallback& onDelta, std::string& outFull, std::string& outError,
                                 AiChatUsage* usage = nullptr, AiChatRequestStats* stats = nullptr,
                                 AiToolReply* toolReply = nullptr);

using AiChatCompletion = std::function<decltype(RunChatCompletionsStreaming)>;

} // namespace HIKARI::EDITOR::AI_ASSISTANT
