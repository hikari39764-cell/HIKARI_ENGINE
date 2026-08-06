#include "HIKARI_AI_VisionClient.h"

#include "Core/Text/HIKARI_Trim.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string_view>
#include <vector>

namespace HIKARI::EDITOR::AI_ASSISTANT {
namespace {

struct VisionFields {
    std::string apiKey;
    std::string baseUrl;
    std::string model;
    bool apiKeySet = false;
    bool baseUrlSet = false;
    bool modelSet = false;
};

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

void ParseFields(const std::string& path,
                 VisionFields& fields,
                 bool allowEmpty) {
    std::ifstream stream(path);
    if (!stream) {
        return;
    }
    std::string line;
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
        if (key == "VISION_API_KEY" && !fields.apiKeySet) {
            fields.apiKey = value;
            fields.apiKeySet = true;
        } else if (key == "VISION_BASE_URL" && !fields.baseUrlSet) {
            fields.baseUrl = value;
            fields.baseUrlSet = true;
        } else if (key == "VISION_MODEL" && !fields.modelSet) {
            fields.model = value;
            fields.modelSet = true;
        } else if (key == "DASHSCOPE_API_KEY" && !fields.apiKeySet) {
            fields.apiKey = value;
            fields.apiKeySet = true;
        } else if (key == "DASHSCOPE_BASE_URL" && !fields.baseUrlSet) {
            fields.baseUrl = value;
            fields.baseUrlSet = true;
        }
    }
}

bool UpsertKey(const std::string& path,
               const std::string& key,
               const std::string& value) {
    std::ifstream input(path);
    std::vector<std::string> lines;
    if (input) {
        std::string line;
        while (std::getline(input, line)) {
            const auto separator = line.find('=');
            const std::string existingKey = separator == std::string::npos
                ? std::string{}
                : Trim(line.substr(0u, separator));
            if (existingKey != key) {
                lines.push_back(line);
            }
        }
    }
    lines.push_back(key + "=" + value);
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        return false;
    }
    for (const std::string& line : lines) {
        output << line << '\n';
    }
    return output.good();
}

bool RemoveKey(const std::string& path, const std::string& key) {
    std::ifstream input(path);
    std::vector<std::string> lines;
    if (!input) {
        return true;
    }
    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        const std::string existingKey = separator == std::string::npos
            ? std::string{}
            : Trim(line.substr(0u, separator));
        if (existingKey != key) {
            lines.push_back(line);
        }
    }
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        return false;
    }
    for (const std::string& line : lines) {
        output << line << '\n';
    }
    return output.good();
}

std::string Base64Encode(const std::string& data) {
    static constexpr char kTable[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((data.size() + 2u) / 3u) * 4u);
    for (size_t index = 0u; index < data.size(); index += 3u) {
        const uint32_t a = static_cast<unsigned char>(data[index]);
        const uint32_t b = index + 1u < data.size()
            ? static_cast<unsigned char>(data[index + 1u])
            : 0u;
        const uint32_t c = index + 2u < data.size()
            ? static_cast<unsigned char>(data[index + 2u])
            : 0u;
        const uint32_t combined = (a << 16u) | (b << 8u) | c;
        result.push_back(kTable[(combined >> 18u) & 0x3fu]);
        result.push_back(kTable[(combined >> 12u) & 0x3fu]);
        result.push_back(index + 1u < data.size()
            ? kTable[(combined >> 6u) & 0x3fu]
            : '=');
        result.push_back(index + 2u < data.size()
            ? kTable[combined & 0x3fu]
            : '=');
    }
    return result;
}

std::string ImageMime(const std::string& imagePath) {
    const auto dot = imagePath.find_last_of('.');
    std::string extension = dot == std::string::npos
        ? std::string{}
        : imagePath.substr(dot + 1u);
    std::transform(
        extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension == "png") return "image/png";
    if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
    if (extension == "webp") return "image/webp";
    if (extension == "bmp") return "image/bmp";
    if (extension == "gif") return "image/gif";
    return "image/png";
}

bool ReadImageDataUri(const std::string& imagePath,
                      std::string& outDataUri) {
    std::ifstream stream(imagePath, std::ios::binary);
    if (!stream) {
        return false;
    }
    const std::string data(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>());
    if (data.empty()) {
        return false;
    }
    outDataUri =
        "data:" + ImageMime(imagePath) + ";base64," +
        Base64Encode(data);
    return true;
}

std::string BuildVisionBody(const AiChatConfig& config,
                            const std::string& prompt,
                            const std::string& dataUri) {
    return
        "{\"model\":\"" + AiJsonEscape(config.model) + "\","
        "\"messages\":[{"
        "\"role\":\"user\","
        "\"content\":["
        "{\"type\":\"text\",\"text\":\"" + AiJsonEscape(prompt) + "\"},"
        "{\"type\":\"image_url\",\"image_url\":{\"url\":\"" +
        AiJsonEscape(dataUri) + "\"}}"
        "]}],\"stream\":false}";
}

} // namespace

bool LoadAiVisionConfig(AiChatConfig& outConfig) {
    outConfig = {};
    VisionFields local;
    ParseFields(GetAiUserConfigPath(), local, true);
    VisionFields environment;
    if (const char* value = std::getenv("VISION_API_KEY")) {
        environment.apiKey = value;
        environment.apiKeySet = true;
    }
    if (const char* value = std::getenv("VISION_BASE_URL")) {
        environment.baseUrl = value;
        environment.baseUrlSet = true;
    }
    if (const char* value = std::getenv("VISION_MODEL")) {
        environment.model = value;
        environment.modelSet = true;
    }
    VisionFields skill;
    ParseFields(GetAiVisionSkillEnvPath(), skill, false);
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
        outConfig.model = "qwen-vl-plus";
    }
    outConfig.valid = !outConfig.apiKey.empty();
    return outConfig.valid;
}

bool SaveAiVisionConfig(const AiChatConfig& config) {
    const std::string path = GetAiUserConfigPath();
    if (path.empty()) {
        return false;
    }
    return UpsertKey(path, "VISION_API_KEY", config.apiKey) &&
        UpsertKey(path, "VISION_BASE_URL", config.baseUrl) &&
        UpsertKey(path, "VISION_MODEL", config.model);
}

bool ResetAiVisionConfig() {
    const std::string path = GetAiUserConfigPath();
    if (path.empty()) {
        return false;
    }
    return RemoveKey(path, "VISION_API_KEY") &&
        RemoveKey(path, "VISION_BASE_URL") &&
        RemoveKey(path, "VISION_MODEL");
}

bool RunVisionAnalysis(
    const AiChatConfig& config,
    const std::string& imagePath,
    const std::string& prompt,
    std::string& outText,
    std::string& outError) {
    outText.clear();
    outError.clear();
    if (!config.valid) {
        outError = "未配置视觉 API Key";
        return false;
    }
    std::string dataUri;
    if (!ReadImageDataUri(imagePath, dataUri)) {
        outError = "无法读取图片：" + imagePath;
        return false;
    }
    const std::string body = BuildVisionBody(
        config,
        prompt.empty() ? "请描述这张图片的内容。" : prompt,
        dataUri);
    return RunApiRequest(config, body, outText, outError);
}

} // namespace HIKARI::EDITOR::AI_ASSISTANT
