#include "HIKARI_AI_ChatClient.h"
#include "HIKARI_AI_HttpTransport.h"

#include "Core/Text/HIKARI_Trim.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <optional>
#include <sstream>

#include <json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace HIKARI::EDITOR::AI_ASSISTANT {
namespace {

std::string Trim(std::string value) {
    return TEXT::TrimCopy(std::move(value));
}

std::string Unquote(std::string value) {
    value = Trim(std::move(value));
    if (value.size() >= 2u &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        value = value.substr(1u, value.size() - 2u);
    }
    return value;
}

std::optional<std::string> GetEnvValue(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

std::string SkillEnvPath() {
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile == nullptr || userProfile[0] == '\0') {
        return {};
    }
    return std::string(userProfile) +
        "/.codex/skills/claude-vision-skill/.env";
}

std::string EngineConfigPath() {
    const char* userProfile = std::getenv("USERPROFILE");
    if (userProfile == nullptr || userProfile[0] == '\0') {
        return {};
    }
    return std::string(userProfile) + "/.hikari-ai-assistant.env";
}

struct EnvFields {
    std::string apiKey;
    std::string baseUrl;
    std::string model;
    bool apiKeySet = false;
    bool baseUrlSet = false;
    bool modelSet = false;
    bool imageInput = true;
    int maxToolRounds = 40;
    int toolHistoryKiB = 24;
    bool requestUsage = true;
};

void ParseEnvFile(const std::string& path,
                  EnvFields& fields,
                  bool allowEmpty) {
    std::ifstream stream(path);
    if (!stream) {
        return;
    }
    std::string line, visionFallback;
    while (std::getline(stream, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = Trim(line.substr(0u, separator));
        const std::string value = Unquote(line.substr(separator + 1u));
        if (key.empty() || (!allowEmpty && value.empty())) {
            continue;
        }
        if (key == "CHAT_IMAGE_INPUT") fields.imageInput = value != "0";
        if (key == "CHAT_MAX_TOOL_ROUNDS") { try { fields.maxToolRounds = std::clamp(std::stoi(value),1,120); } catch (...) {} }
        if (key == "CHAT_TOOL_HISTORY_KIB") { try { fields.toolHistoryKiB = std::clamp(std::stoi(value),8,256); } catch (...) {} }
        if (key == "CHAT_REQUEST_USAGE") fields.requestUsage = value != "0";
        if (key == "DASHSCOPE_API_KEY" && !fields.apiKeySet) {
            fields.apiKey = value;
            fields.apiKeySet = true;
        } else if (key == "DASHSCOPE_BASE_URL" && !fields.baseUrlSet) {
            fields.baseUrl = value;
            fields.baseUrlSet = true;
        } else if (key == "CHAT_MODEL" && !fields.modelSet) {
            fields.model = value;
            fields.modelSet = true;
        } else if (!allowEmpty && key == "VISION_MODEL") {
            visionFallback = value;
        }
    }
    // skill の旧設定だけを fallback とする。保存順で主モデルが
    // 独立した VISION_MODEL に置き換わることはない。
    if (!fields.modelSet && !visionFallback.empty()) { fields.model=visionFallback; fields.modelSet=true; }
}

std::string JsonEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 16u);
    for (const char c : value) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20u) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                result += buffer;
            } else {
                result += c;
            }
        }
    }
    return result;
}

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

bool LoadAiChatConfig(AiChatConfig& outConfig) {
    outConfig = {};
    // Precedence: engine-local config file (written by the AI Assistant UI)
    // > process environment variables > the vision-skill .env fallback.
    EnvFields local;
    ParseEnvFile(EngineConfigPath(), local, true);
    outConfig.imageInputEnabled = local.imageInput;
    outConfig.maxToolRounds = local.maxToolRounds;
    outConfig.toolHistoryKiB = local.toolHistoryKiB;
    outConfig.requestUsage = local.requestUsage;
    EnvFields environment;
    if (const auto value = GetEnvValue("DASHSCOPE_API_KEY")) {
        environment.apiKey = *value;
        environment.apiKeySet = true;
    }
    if (const auto value = GetEnvValue("DASHSCOPE_BASE_URL")) {
        environment.baseUrl = *value;
        environment.baseUrlSet = true;
    }
    if (const auto value = GetEnvValue("CHAT_MODEL")) {
        environment.model = *value;
        environment.modelSet = true;
    }
    EnvFields skill;
    ParseEnvFile(SkillEnvPath(), skill, false);
    outConfig.apiKey = local.apiKeySet
        ? local.apiKey
        : environment.apiKeySet ? environment.apiKey : skill.apiKey;
    outConfig.baseUrl = local.baseUrlSet
        ? local.baseUrl
        : environment.baseUrlSet ? environment.baseUrl : skill.baseUrl;
    outConfig.model = local.modelSet
        ? local.model
        : environment.modelSet ? environment.model : skill.model;
    outConfig.apiKey = Trim(std::move(outConfig.apiKey));
    outConfig.baseUrl = Trim(std::move(outConfig.baseUrl));
    outConfig.model = Trim(std::move(outConfig.model));
    if (outConfig.baseUrl.empty()) {
        outConfig.baseUrl =
            "https://dashscope.aliyuncs.com/compatible-mode/v1";
    }
    if (outConfig.model.empty()) {
        outConfig.model = "qwen-plus";
    }
    outConfig.valid = !outConfig.apiKey.empty();
    return outConfig.valid;
}

namespace {
bool RewriteChatConfig(const AiChatConfig* config) {
    const std::string path=EngineConfigPath();
    if (path.empty()) return false;
    std::vector<std::string> preserved;
    std::ifstream input(path);
    if (!input && std::filesystem::exists(path)) return false;
    for (std::string line;std::getline(input,line);) {
        const auto key=Trim(line.substr(0,line.find('=')));
        if (key!="DASHSCOPE_API_KEY" && key!="DASHSCOPE_BASE_URL" && key!="CHAT_MODEL" &&
            key!="CHAT_IMAGE_INPUT" && key!="CHAT_MAX_TOOL_ROUNDS" &&
            key!="CHAT_TOOL_HISTORY_KIB" && key!="CHAT_REQUEST_USAGE") preserved.push_back(line);
    }
    input.close();
    const auto temporary=path+".tmp";
    std::ofstream output(temporary,std::ios::trunc);
    for (const auto& line:preserved) output<<line<<'\n';
    if (config) {
        output<<"DASHSCOPE_API_KEY="<<config->apiKey<<'\n'<<"DASHSCOPE_BASE_URL="<<config->baseUrl<<'\n'
            <<"CHAT_MODEL="<<config->model<<'\n'<<"CHAT_IMAGE_INPUT="<<(config->imageInputEnabled?1:0)<<'\n'
            <<"CHAT_MAX_TOOL_ROUNDS="<<std::clamp(config->maxToolRounds,1,120)<<'\n'
            <<"CHAT_TOOL_HISTORY_KIB="<<std::clamp(config->toolHistoryKiB,8,256)<<'\n'
            <<"CHAT_REQUEST_USAGE="<<(config->requestUsage?1:0)<<'\n';
    }
    output.flush();if (!output) return false;output.close();
#ifdef _WIN32
    return MoveFileExW(std::filesystem::path(temporary).c_str(),std::filesystem::path(path).c_str(),
        MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
    std::error_code error;std::filesystem::rename(temporary,path,error);return !error;
#endif
}
}
bool SaveAiChatConfig(const AiChatConfig& config) { return RewriteChatConfig(&config); }
bool ResetAiChatConfig() { return RewriteChatConfig(nullptr); }

std::string GetAiUserConfigPath() {
    return EngineConfigPath();
}

std::string GetAiVisionSkillEnvPath() {
    return SkillEnvPath();
}

std::string AiJsonEscape(const std::string& value) {
    return JsonEscape(value);
}

std::string ExtractJsonField(std::string_view text,std::string_view field) {
    const auto value=nlohmann::json::parse(text,nullptr,false);
    if (!value.is_object()) return {};
    const auto found=value.find(std::string(field));
    return found!=value.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

std::string ExtractJsonNumberField(
    std::string_view json,
    std::string_view field) {
    const std::string marker =
        std::string("\"") + std::string(field) + "\"";
    const auto position = json.find(marker);
    if (position == std::string_view::npos) {
        return {};
    }
    const auto colon = json.find(':', position + marker.size());
    if (colon == std::string_view::npos) {
        return {};
    }
    size_t cursor = colon + 1u;
    while (cursor < json.size() &&
           (json[cursor] == ' ' || json[cursor] == '\t' ||
            json[cursor] == '\r' || json[cursor] == '\n')) {
        ++cursor;
    }
    const size_t start = cursor;
    if (cursor < json.size() &&
        (json[cursor] == '-' || json[cursor] == '+')) {
        ++cursor;
    }
    while (cursor < json.size() &&
           (std::isdigit(static_cast<unsigned char>(json[cursor])) ||
            json[cursor] == '.' || json[cursor] == 'e' ||
            json[cursor] == 'E' || json[cursor] == '-' ||
            json[cursor] == '+')) {
        ++cursor;
    }
    if (cursor == start) {
        return {};
    }
    return std::string(json.substr(start, cursor - start));
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
    std::string& outError, AiChatUsage* usage, AiChatRequestStats* stats) {
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
    return true;
}

std::vector<std::string> ExtractAllActionJson(
    const std::string& reply) {
    std::vector<std::string> actions;
    const auto isPlausibleAction = [](std::string_view name) {
        if (name.empty() || name.size() > 64u) {
            return false;
        }
        for (const char c : name) {
            const bool valid =
                (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '.' || c == '_' || c == '-';
            if (!valid) {
                return false;
            }
        }
        return true;
    };
    const auto collectObject =
        [&actions, &isPlausibleAction](const nlohmann::json& node) {
            if (node.is_object() && node.contains("action") &&
                node["action"].is_string()) {
                const std::string name =
                    node["action"].get<std::string>();
                if (isPlausibleAction(name)) {
                    actions.push_back(node.dump());
                }
            }
        };
    const auto collectContainer =
        [&collectObject](const nlohmann::json& node) {
            if (node.is_object()) {
                if (node.contains("actions") &&
                    node["actions"].is_array()) {
                    for (const auto& item : node["actions"]) {
                        collectObject(item);
                    }
                } else {
                    collectObject(node);
                }
            } else if (node.is_array()) {
                for (const auto& item : node) {
                    collectObject(item);
                }
            }
        };

    // Whole-document form: {"actions":[...]} or an action array.
    try {
        collectContainer(nlohmann::json::parse(reply));
        if (!actions.empty()) {
            return actions;
        }
    } catch (...) {
    }

    // Whole-line form: one complete JSON object per line.
    size_t start = 0u;
    while (start < reply.size()) {
        const auto newline = reply.find('\n', start);
        const std::string line = newline == std::string::npos
            ? reply.substr(start)
            : reply.substr(start, newline - start);
        start = newline == std::string::npos
            ? reply.size()
            : newline + 1u;
        const std::string trimmed = Trim(line);
        if (trimmed.size() < 2u ||
            trimmed.front() != '{' ||
            trimmed.back() != '}') {
            continue;
        }
        try {
            collectContainer(nlohmann::json::parse(trimmed));
        } catch (...) {
        }
    }
    if (!actions.empty()) {
        return actions;
    }

    // Fenced block form: ```json ... ```.
    size_t cursor = 0u;
    while (cursor < reply.size()) {
        const auto fence = reply.find("```", cursor);
        if (fence == std::string::npos) {
            break;
        }
        const auto close = reply.find("```", fence + 3u);
        if (close == std::string::npos) {
            break;
        }
        std::string block =
            reply.substr(fence + 3u, close - fence - 3u);
        cursor = close + 3u;
        const auto firstNewline = block.find('\n');
        if (firstNewline != std::string::npos) {
            const std::string tag =
                Trim(block.substr(0u, firstNewline));
            if (tag == "json" || tag == "JSON") {
                block = block.substr(firstNewline + 1u);
            }
        }
        try {
            collectContainer(nlohmann::json::parse(block));
        } catch (...) {
        }
    }

    if (!actions.empty()) return actions;

    // Brace-pairing fallback: the model may mix JSON with prose or end the
    // object with punctuation. Scan for balanced {...} candidates, parse each
    // with nlohmann, and keep only objects with a plausible action key.
    cursor = 0u;
    while (cursor < reply.size()) {
        const auto open = reply.find('{', cursor);
        if (open == std::string::npos) {
            break;
        }
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        size_t close = open;
        for (; close < reply.size(); ++close) {
            const char c = reply[close];
            if (inString) {
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    inString = false;
                }
                continue;
            }
            if (c == '"') {
                inString = true;
            } else if (c == '{') {
                ++depth;
            } else if (c == '}') {
                --depth;
                if (depth == 0) {
                    break;
                }
            }
        }
        if (close >= reply.size()) {
            break;
        }
        const std::string candidate =
            reply.substr(open, close - open + 1u);
        cursor = close + 1u;
        if (candidate.size() > 4096u) {
            continue;
        }
        try {
            collectContainer(nlohmann::json::parse(candidate));
        } catch (...) {
        }
    }
    return actions;
}

} // namespace HIKARI::EDITOR::AI_ASSISTANT
