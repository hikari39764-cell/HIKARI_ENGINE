#include "HIKARI_AI_ChatClient.h"

#include <algorithm>
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

void ParseEnvFile(const std::string& path,
                  std::string& apiKey,
                  std::string& baseUrl,
                  std::string& model) {
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
        if (key.empty() || value.empty()) {
            continue;
        }
        if (key == "DASHSCOPE_API_KEY" && apiKey.empty()) {
            apiKey = value;
        } else if (key == "DASHSCOPE_BASE_URL" && baseUrl.empty()) {
            baseUrl = value;
        } else if (key == "CHAT_MODEL" && model.empty()) {
            model = value;
        } else if (key == "VISION_MODEL" && model.empty()) {
            model = value;
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
    const std::string prefix = "https://";
    if (url.rfind(prefix, 0u) == 0u) {
        url = url.substr(prefix.size());
    } else if (url.rfind("http://", 0u) == 0u) {
        url = url.substr(7u);
    }
    const auto slash = url.find('/');
    const std::string host = slash == std::string::npos
        ? url
        : url.substr(0u, slash);
    std::string path = slash == std::string::npos
        ? "/compatible-mode/v1/chat/completions"
        : url.substr(slash);
    if (path.empty() || path.back() != '/') {
        path += '/';
    }
    path += "chat/completions";
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
    std::string apiKey;
    std::string baseUrl;
    std::string model;
    if (const auto value = GetEnvValue("DASHSCOPE_API_KEY")) {
        apiKey = *value;
    }
    if (const auto value = GetEnvValue("DASHSCOPE_BASE_URL")) {
        baseUrl = *value;
    }
    if (const auto value = GetEnvValue("CHAT_MODEL")) {
        model = *value;
    }
    ParseEnvFile(SkillEnvPath(), apiKey, baseUrl, model);
    outConfig.apiKey = Trim(std::move(apiKey));
    outConfig.baseUrl = Trim(std::move(baseUrl));
    if (outConfig.baseUrl.empty()) {
        outConfig.baseUrl =
            "https://dashscope.aliyuncs.com/compatible-mode/v1";
    }
    outConfig.model = Trim(std::move(model));
    if (outConfig.model.empty()) {
        outConfig.model = "qwen-plus";
    }
    outConfig.valid = !outConfig.apiKey.empty();
    return outConfig.valid;
}

bool RunChatCompletions(
    const AiChatConfig& config,
    const std::vector<AiChatMessage>& messages,
    std::string& outContent,
    std::string& outError) {
    outContent.clear();
    outError.clear();
    if (!config.valid) {
        outError = "未配置 DASHSCOPE_API_KEY";
        return false;
    }
#ifdef _WIN32
    std::wstring host;
    std::wstring path;
    if (!SplitEndpoint(config.baseUrl, host, path)) {
        outError = "无法解析 DashScope 端点：" + config.baseUrl;
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
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    if (response.empty()) {
        outError = "DashScope 返回空响应";
        return false;
    }
    if (response.find("\"error\"") != std::string::npos) {
        outError = "DashScope 返回错误：" +
            ExtractJsonContent(response);
        if (outError.empty()) {
            outError = "DashScope 返回错误（响应被截断）";
        }
        return false;
    }
    outContent = ExtractJsonContent(response);
    return !outContent.empty();
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
