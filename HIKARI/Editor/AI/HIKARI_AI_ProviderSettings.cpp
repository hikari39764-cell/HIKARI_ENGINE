#include "HIKARI_AI_ChatClient.h"
#include "Core/Text/HIKARI_Trim.h"
#include <algorithm>
#include <cstdlib>
#include <climits>
#include <filesystem>
#include <fstream>
#include <map>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace HIKARI::EDITOR::AI_ASSISTANT {
namespace {
using Values = std::map<std::string, std::string>;
Values ReadSettings() {
    Values values;
    std::ifstream input(GetAiUserConfigPath());
    for (std::string line; std::getline(input, line);) {
        const auto equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        auto key = TEXT::TrimCopy(line.substr(0, equals));
        auto value = TEXT::TrimCopy(line.substr(equals + 1));
        if (value.size() >= 2 &&
            ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
            value = value.substr(1, value.size() - 2);
        if (!key.empty())
            values.try_emplace(std::move(key), std::move(value));
    }
    return values;
}
std::string Setting(const Values& values, const char* name, const char* fallback = "") {
    if (const auto found = values.find(name); found != values.end())
        return found->second;
    if (const char* value = std::getenv(name); value && *value)
        return TEXT::TrimCopy(std::string(value));
    return fallback;
}
int Integer(const Values& values, const char* name, int fallback, int low, int high) {
    try {
        return std::clamp(std::stoi(Setting(values, name)), low, high);
    } catch (...) {
        return fallback;
    }
}
bool Load(AiChatConfig& config, bool vision) {
    config = {};
    const auto values = ReadSettings();
    config.apiKey = Setting(values, vision ? "VISION_API_KEY" : "DASHSCOPE_API_KEY");
    config.baseUrl = Setting(values, vision ? "VISION_BASE_URL" : "DASHSCOPE_BASE_URL");
    config.model = Setting(values, vision ? "VISION_MODEL" : "CHAT_MODEL");
    if (vision) {
        if (config.apiKey.empty() && !values.contains("VISION_API_KEY"))
            config.apiKey = Setting(values, "DASHSCOPE_API_KEY");
        if (config.baseUrl.empty())
            config.baseUrl = Setting(values, "DASHSCOPE_BASE_URL");
    }
    if (config.baseUrl.empty())
        config.baseUrl = "https://dashscope.aliyuncs.com/compatible-mode/v1";
    if (config.model.empty())
        config.model = vision ? "qwen-vl-plus" : "qwen-plus";
    config.imageInputEnabled = vision || Setting(values, "CHAT_IMAGE_INPUT", "1") != "0";
    config.requestUsage = Setting(values, "CHAT_REQUEST_USAGE", "1") != "0";
    config.maxToolRounds = Integer(values, "CHAT_MAX_TOOL_ROUNDS", kDefaultAiToolRounds, 1, kMaxAiToolRounds);
    config.maxTaskTokens = Integer(values, "CHAT_MAX_TASK_TOKENS", 0, 0, INT_MAX);
    config.maxNoProgressRounds = Integer(values, "CHAT_MAX_NO_PROGRESS_ROUNDS", 12, 0, kMaxAiToolRounds);
    config.toolHistoryKiB = Integer(values, "CHAT_TOOL_HISTORY_KIB", 24, 8, 256);
    config.valid = !config.apiKey.empty();
    return config.valid;
}
bool Save(const AiChatConfig* config, bool vision) {
    const auto path = GetAiUserConfigPath();
    if (path.empty())
        return false;
    auto values = ReadSettings();
    std::erase_if(values, [vision](const auto& entry) {
        const auto& key = entry.first;
        return vision ? key.starts_with("VISION_")
                      : key.starts_with("CHAT_") || key == "DASHSCOPE_API_KEY" || key == "DASHSCOPE_BASE_URL";
    });
    if (config) {
        values[vision ? "VISION_API_KEY" : "DASHSCOPE_API_KEY"] = config->apiKey;
        values[vision ? "VISION_BASE_URL" : "DASHSCOPE_BASE_URL"] = config->baseUrl;
        values[vision ? "VISION_MODEL" : "CHAT_MODEL"] = config->model;
        if (!vision) {
            values["CHAT_IMAGE_INPUT"] = config->imageInputEnabled ? "1" : "0";
            values["CHAT_REQUEST_USAGE"] = config->requestUsage ? "1" : "0";
            values["CHAT_MAX_TOOL_ROUNDS"] = std::to_string(std::clamp(config->maxToolRounds, 1, kMaxAiToolRounds));
            values["CHAT_MAX_TASK_TOKENS"] = std::to_string((std::max)(0, config->maxTaskTokens));
            values["CHAT_MAX_NO_PROGRESS_ROUNDS"] =
                std::to_string(std::clamp(config->maxNoProgressRounds, 0, kMaxAiToolRounds));
            values["CHAT_TOOL_HISTORY_KIB"] = std::to_string(std::clamp(config->toolHistoryKiB, 8, 256));
        }
    }
    const auto temporary = path + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    for (const auto& [key, value] : values) {
        if (value.find_first_of("\r\n") != std::string::npos)
            return false;
        output << key << '=' << value << '\n';
    }
    output.flush();
    if (!output)
        return false;
    output.close();
#ifdef _WIN32
    return MoveFileExW(std::filesystem::path(temporary).c_str(), std::filesystem::path(path).c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    return !error;
#endif
}
} // namespace
std::string GetAiUserConfigPath() {
    const char* profile = std::getenv("USERPROFILE");
    return profile && *profile ? std::string(profile) + "/.hikari-ai-assistant.env" : std::string{};
}
bool LoadAiChatConfig(AiChatConfig& config) { return Load(config, false); }
bool LoadAiVisionConfig(AiChatConfig& config) { return Load(config, true); }
bool SaveAiChatConfig(const AiChatConfig& config) { return Save(&config, false); }
bool SaveAiVisionConfig(const AiChatConfig& config) { return Save(&config, true); }
bool ResetAiChatConfig() { return Save(nullptr, false); }
bool ResetAiVisionConfig() { return Save(nullptr, true); }
} // namespace HIKARI::EDITOR::AI_ASSISTANT
