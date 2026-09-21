#include "HIKARI_AI_ToolProtocol.h"
#include "HIKARI_AI_ChatClient.h"
#include "HIKARI_AI_HttpTransport.h"
#include "HIKARI_AI_MessageText.h"

#include <utility>
#include <json.hpp>

namespace HIKARI::EDITOR::AI_ASSISTANT {
namespace {

std::string ExtractJsonContent(const std::string& response) {
    const auto parsed=nlohmann::json::parse(response,nullptr,false);
    if (!parsed.is_object() || !parsed.contains("choices") || !parsed["choices"].is_array() || parsed["choices"].empty()) return {};
    const auto& choice=parsed["choices"][0];
    const auto found=choice.find(choice.contains("delta") ? "delta" : "message");
    if (found==choice.end() || !found->is_object()) return {};
    const auto content=found->find("content");
    return content!=found->end() && content->is_string() ? content->get<std::string>() : std::string{};
}

void ReadUsage(const std::string& response, AiChatUsage* output) {
    if (!output) return;
    const auto root = nlohmann::json::parse(response, nullptr, false);
    if (!root.is_object() || !root.contains("usage") || !root["usage"].is_object()) return;
    const auto& usage = root["usage"];
    const auto read = [](const nlohmann::json& object, const char* key, int64_t& value) {
        const auto it = object.find(key);
        if (it != object.end() && it->is_number_integer() && it->get<int64_t>() >= 0)
            value = it->get<int64_t>();
    };
    read(usage, "prompt_tokens", output->inputTokens);
    read(usage, "completion_tokens", output->outputTokens);
    read(usage, "prompt_cache_hit_tokens", output->cachedTokens);
    if (usage.contains("prompt_tokens_details") && usage["prompt_tokens_details"].is_object())
        read(usage["prompt_tokens_details"], "cached_tokens", output->cachedTokens);
}

} // namespace

std::string AiJsonEscape(const std::string& value) {
    const auto quoted = nlohmann::json(value).dump();
    return quoted.substr(1,quoted.size()-2);
}

std::string ExtractJsonField(std::string_view text,std::string_view field) {
    const auto value=nlohmann::json::parse(text,nullptr,false);
    if (!value.is_object()) return {};
    const auto found=value.find(std::string(field));
    return found!=value.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

std::string ExtractJsonNumberField(std::string_view text, std::string_view field) {
    const auto value = nlohmann::json::parse(text,nullptr,false);
    if (!value.is_object()) return {};
    const auto found = value.find(std::string(field));
    return found != value.end() && found->is_number() ? found->dump() : std::string{};
}

bool RunApiRequest(
    const AiChatConfig& config,
    const std::string& body,
    std::string& outContent,
    std::string& outError, AiChatUsage* usage) {
    if (usage) *usage = {};
    std::string raw;
    if (!RunAiHttpStream(config,body,[&raw](const std::string& chunk) { raw+=chunk;return raw.size()<8*1024*1024; },outError)) {
        return false;
    }
    outContent = ExtractJsonContent(raw);
    ReadUsage(raw, usage);
    if (outContent.empty()) {
        outError = "API 响应中没有 content 字段";
        return false;
    }
    return true;
}

bool RunChatCompletionsStreaming(
    const AiChatConfig& config,
    const std::vector<AiChatMessage>& messages,
    const AiChatStreamCallback& onDelta,
    std::string& outFull,
    std::string& outError, AiChatUsage* usage, AiChatRequestStats* stats, AiToolReply* toolReply) {
    outFull.clear();
    outError.clear();
    if (usage) *usage = {};
    if (stats) *stats = {};
    if (!config.valid) {
        outError = "未配置 API Key（DASHSCOPE_API_KEY）";
        return false;
    }
    std::string body;
    if (!BuildAiChatRequest(config, messages, true, body, outError, stats)) return false;
    AiToolReply decoded;
    std::string full;
    std::string pending;
    bool sawDone = false;
    bool userAbort = false;
    const auto parsePending = [&]() {
        size_t newline = 0u;
        while ((newline = pending.find('\n')) !=
               std::string::npos) {
            const std::string line = pending.substr(0u, newline);
            pending.erase(0u, newline + 1u);
            const std::string trimmed = Trim(line);
            if (trimmed.rfind("data:", 0u) != 0u) {
                continue;
            }
            const std::string data = Trim(trimmed.substr(5u));
            if (data == "[DONE]") {
                sawDone = true;
                return false;
            }
            ReadUsage(data, usage);
            decoded.ReadDelta(nlohmann::json::parse(data, nullptr, false));
            const std::string delta = ExtractJsonContent(data);
            if (delta.empty()) {
                continue;
            }
            full += delta;
            if (onDelta && !onDelta(delta)) {
                userAbort = true;
                return false;
            }
        }
        return true;
    };
    const auto onChunk = [&](const std::string& chunk) {
        if (!chunk.empty()) {
            pending += chunk;
        }
        return parsePending();
    };
    std::string streamError;
    const bool streamOk = RunAiHttpStream(
        config, body, onChunk, streamError);
    // Flush any trailing partial SSE line (for example when the stream times
    // out before a final newline) so a complete action is not silently lost.
    (void)onChunk(std::string{});
    if (userAbort) {
        outError = "用户中止";
        return false;
    }
    if (!streamOk || !sawDone) {
        outError=streamError.empty() ? "响应未完整结束，未执行其中的动作。" : streamError;
        outFull=std::move(full);return false;
    }
    outFull=std::move(full);
    if (config.editorTools && !decoded.Validate(outError)) return false;
    if (toolReply) *toolReply = std::move(decoded);
    return true;
}

bool RunVisionAnalysis(const AiChatConfig& config, const std::string& imagePath,
    const std::string& prompt, std::string& outText, std::string& outError) {
    auto vision = config;
    vision.editorTools = false;
    std::string body;
    const std::vector<AiChatMessage> messages{{"user",prompt.empty() ? "Describe the image." : prompt,{{imagePath,"Preview"}}}};
    return BuildAiChatRequest(vision,messages,false,body,outError) && RunApiRequest(vision,body,outText,outError);
}

} // namespace HIKARI::EDITOR::AI_ASSISTANT
