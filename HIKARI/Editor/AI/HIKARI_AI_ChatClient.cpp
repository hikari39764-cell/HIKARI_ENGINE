#include "HIKARI_AI_ChatClient.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace HIKARI::EDITOR::AI_ASSISTANT {
namespace {

std::string Trim(std::string value) {
    const auto notSpace = [](unsigned char c) {
        return std::isspace(c) == 0;
    };
    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), notSpace));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), notSpace).base(),
        value.end());
    return value;
}

std::wstring WideFromAscii(const std::string& value) {
    std::wstring result;
    result.reserve(value.size());
    for (const char c : value) {
        result.push_back(static_cast<wchar_t>(
            static_cast<unsigned char>(c)));
    }
    return result;
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
};

void ParseEnvFile(const std::string& path,
                  EnvFields& fields,
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
        if (key == "DASHSCOPE_API_KEY" && !fields.apiKeySet) {
            fields.apiKey = value;
            fields.apiKeySet = true;
        } else if (key == "DASHSCOPE_BASE_URL" && !fields.baseUrlSet) {
            fields.baseUrl = value;
            fields.baseUrlSet = true;
        } else if (key == "CHAT_MODEL" && !fields.modelSet) {
            fields.model = value;
            fields.modelSet = true;
        } else if (key == "VISION_MODEL" && !fields.modelSet) {
            fields.model = value;
            fields.modelSet = true;
        }
    }
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

std::string BuildRequestBody(
    const AiChatConfig& config,
    const std::vector<AiChatMessage>& messages) {
    std::ostringstream body;
    body << "{\"model\":\"" << JsonEscape(config.model)
         << "\",\"messages\":[";
    for (size_t index = 0u; index < messages.size(); ++index) {
        if (index > 0u) {
            body << ',';
        }
        body << "{\"role\":\"" << JsonEscape(messages[index].role)
             << "\",\"content\":\""
             << JsonEscape(messages[index].content) << "\"}";
    }
    body << "],\"stream\":false}";
    return body.str();
}

bool SplitEndpoint(
    const std::string& baseUrl,
    std::wstring& outHost,
    std::wstring& outPath) {
    std::string url = baseUrl;
    if (url.rfind("https://", 0u) == 0u) {
        url = url.substr(8u);
    } else if (url.rfind("http://", 0u) == 0u) {
        url = url.substr(7u);
    }
    const auto slash = url.find('/');
    const std::string host = slash == std::string::npos
        ? url
        : url.substr(0u, slash);
    std::string path = slash == std::string::npos
        ? std::string{}
        : url.substr(slash);
    constexpr std::string_view kChatCompletions = "/chat/completions";
    if (path.empty()) {
        path = std::string(kChatCompletions);
    } else if (path.back() == '/') {
        path += std::string(kChatCompletions);
    } else if (path.size() < kChatCompletions.size() ||
               path.compare(
                   path.size() - kChatCompletions.size(),
                   kChatCompletions.size(),
                   kChatCompletions) != 0) {
        path += std::string(kChatCompletions);
    }
    if (host.empty()) {
        return false;
    }
    outHost.assign(host.begin(), host.end());
    outPath.assign(path.begin(), path.end());
    return true;
}

std::string ExtractJsonContent(const std::string& response) {
    const std::string marker = "\"content\":\"";
    const auto start = response.find(marker);
    if (start == std::string::npos) {
        return {};
    }
    const auto contentStart = start + marker.size();
    std::string content;
    bool escaped = false;
    for (size_t index = contentStart; index < response.size(); ++index) {
        const char c = response[index];
        if (escaped) {
            switch (c) {
            case 'n': content += '\n'; break;
            case 'r': content += '\r'; break;
            case 't': content += '\t'; break;
            case '"': content += '"'; break;
            case '\\': content += '\\'; break;
            default: content += c;
            }
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            break;
        }
        content += c;
    }
    return content;
}

} // namespace

bool LoadAiChatConfig(AiChatConfig& outConfig) {
    outConfig = {};
    // Precedence: engine-local config file (written by the AI Assistant UI)
    // > process environment variables > the vision-skill .env fallback.
    EnvFields local;
    ParseEnvFile(EngineConfigPath(), local, true);
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

bool SaveAiChatConfig(const AiChatConfig& config) {
    const std::string path = EngineConfigPath();
    if (path.empty()) {
        return false;
    }
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream << "DASHSCOPE_API_KEY=" << config.apiKey << '\n';
    stream << "DASHSCOPE_BASE_URL=" << config.baseUrl << '\n';
    stream << "CHAT_MODEL=" << config.model << '\n';
    return stream.good();
}

bool ResetAiChatConfig() {
    const std::string path = EngineConfigPath();
    if (path.empty()) {
        return false;
    }
    return std::remove(path.c_str()) == 0 || errno == ENOENT;
}

bool RunChatCompletions(
    const AiChatConfig& config,
    const std::vector<AiChatMessage>& messages,
    std::string& outContent,
    std::string& outError) {
    outContent.clear();
    outError.clear();
    if (!config.valid) {
        outError = "未配置 API Key（DASHSCOPE_API_KEY）";
        return false;
    }
#ifdef _WIN32
    std::wstring host;
    std::wstring path;
    if (!SplitEndpoint(config.baseUrl, host, path)) {
        outError = "无法解析 API 端点：" + config.baseUrl;
        return false;
    }
    const std::string body = BuildRequestBody(config, messages);

    HINTERNET session = WinHttpOpen(
        L"HIKARI AI Assistant",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (session == nullptr) {
        outError = "WinHttpOpen 失败";
        return false;
    }
    HINTERNET connection = WinHttpConnect(
        session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connection == nullptr) {
        WinHttpCloseHandle(session);
        outError = "WinHttpConnect 失败";
        return false;
    }
    HINTERNET request = WinHttpOpenRequest(
        connection,
        L"POST",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (request == nullptr) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        outError = "WinHttpOpenRequest 失败";
        return false;
    }

    const std::wstring headers =
        L"Content-Type: application/json\r\n"
        L"Authorization: Bearer " +
        WideFromAscii(config.apiKey) +
        L"\r\n";
    const BOOL sent = WinHttpSendRequest(
        request,
        headers.c_str(),
        static_cast<DWORD>(-1L),
        const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),
        0);
    if (!sent) {
        const DWORD error = GetLastError();
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        outError = "WinHttpSendRequest 失败 (error " +
            std::to_string(error) + ")";
        return false;
    }
    if (!WinHttpReceiveResponse(request, nullptr)) {
        const DWORD error = GetLastError();
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        outError = "WinHttpReceiveResponse 失败 (error " +
            std::to_string(error) + ")";
        return false;
    }

    std::string response;
    char buffer[4096];
    DWORD bytesRead = 0u;
    while (WinHttpReadData(
               request, buffer, static_cast<DWORD>(sizeof(buffer)),
               &bytesRead) &&
           bytesRead > 0u) {
        response.append(buffer, bytesRead);
    }
    DWORD statusCode = 0u;
    DWORD statusSize = sizeof(statusCode);
    (void)WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    const std::string statusText =
        "HTTP " + std::to_string(statusCode) + " ";
    if (response.empty()) {
        outError = "API 返回空响应（" + statusText + "）";
        return false;
    }
    if (response.find("\"error\"") != std::string::npos) {
        const std::string apiMessage = ExtractJsonContent(response);
        outError = apiMessage.empty()
            ? "API 返回错误（" + statusText + "响应：" +
                response.substr(0u, 240u) + "）"
            : "API 返回错误：" + apiMessage;
        return false;
    }
    if (statusCode >= 400u) {
        outError = statusText + "响应：" +
            response.substr(0u, 240u);
        return false;
    }
    outContent = ExtractJsonContent(response);
    if (outContent.empty()) {
        outError = "API 响应中没有 content 字段（" + statusText +
            "响应：" + response.substr(0u, 240u) + "）";
        return false;
    }
    return true;
#else
    (void)messages;
    outError = "仅支持 Windows 编辑器宿主";
    return false;
#endif
}

std::string ExtractActionJson(const std::string& reply) {
    const std::string marker = "{\"action\"";
    const auto start = reply.find(marker);
    if (start == std::string::npos) {
        return {};
    }
    const auto end = reply.find('}', start + 1u);
    if (end == std::string::npos) {
        return {};
    }
    return reply.substr(start, end - start + 1u);
}

} // namespace HIKARI::EDITOR::AI_ASSISTANT
