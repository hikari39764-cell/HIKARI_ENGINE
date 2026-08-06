#include "HIKARI_AI_ChatClient.h"

#include "Core/Text/HIKARI_Trim.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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
    const std::vector<AiChatMessage>& messages,
    bool stream = false) {
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
    body << "],\"stream\":"
         << (stream ? "true" : "false") << "}";
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

std::string GetAiUserConfigPath() {
    return EngineConfigPath();
}

std::string GetAiVisionSkillEnvPath() {
    return SkillEnvPath();
}

std::string AiJsonEscape(const std::string& value) {
    return JsonEscape(value);
}

std::string ExtractJsonField(
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
    const auto quote = json.find('"', colon + 1u);
    if (quote == std::string_view::npos) {
        return {};
    }
    const auto end = json.find('"', quote + 1u);
    if (end == std::string_view::npos) {
        return {};
    }
    return std::string(json.substr(quote + 1u, end - quote - 1u));
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

namespace {

#ifdef _WIN32
struct ApiRequestHandleSet {
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;

    ~ApiRequestHandleSet() {
        if (request != nullptr) {
            WinHttpCloseHandle(request);
        }
        if (connection != nullptr) {
            WinHttpCloseHandle(connection);
        }
    }
};
#else
struct ApiRequestHandleSet {};
#endif

} // namespace

bool BeginApiRequest(
    const AiChatConfig& config,
    const std::string& body,
    DWORD receiveTimeoutMs,
    ApiRequestHandleSet& handles,
    std::string& outError) {
    if (!config.valid) {
        outError = "未配置 API Key";
        return false;
    }
#ifdef _WIN32
    std::wstring host;
    std::wstring path;
    if (!SplitEndpoint(config.baseUrl, host, path)) {
        outError = "无法解析 API 端点：" + config.baseUrl;
        return false;
    }

    static HINTERNET session = WinHttpOpen(
        L"HIKARI AI Assistant",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (session == nullptr) {
        outError = "WinHttpOpen 失败";
        return false;
    }
    (void)WinHttpSetTimeouts(
        session, 5000, 5000, 30000, 90000);
    handles.connection = WinHttpConnect(
        session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (handles.connection == nullptr) {
        outError = "WinHttpConnect 失败";
        return false;
    }
    handles.request = WinHttpOpenRequest(
        handles.connection,
        L"POST",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (handles.request == nullptr) {
        outError = "WinHttpOpenRequest 失败";
        return false;
    }
    (void)WinHttpSetOption(
        handles.request,
        WINHTTP_OPTION_RECEIVE_TIMEOUT,
        &receiveTimeoutMs,
        sizeof(receiveTimeoutMs));

    const std::wstring headers =
        L"Content-Type: application/json\r\n"
        L"Authorization: Bearer " +
        WideFromAscii(config.apiKey) +
        L"\r\n";
    const BOOL sent = WinHttpSendRequest(
        handles.request,
        headers.c_str(),
        static_cast<DWORD>(-1L),
        const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),
        0);
    if (!sent) {
        const DWORD error = GetLastError();
        outError = "WinHttpSendRequest 失败 (error " +
            std::to_string(error) + ")";
        return false;
    }
    if (!WinHttpReceiveResponse(handles.request, nullptr)) {
        const DWORD error = GetLastError();
        outError = "WinHttpReceiveResponse 失败 (error " +
            std::to_string(error) + ")";
        return false;
    }
    return true;
#else
    (void)body;
    (void)receiveTimeoutMs;
    (void)handles;
    outError = "仅支持 Windows 编辑器宿主";
    return false;
#endif
}

bool RunApiRequestRaw(
    const AiChatConfig& config,
    const std::string& body,
    std::string& outRaw,
    std::string& outError) {
    outRaw.clear();
    outError.clear();
#ifdef _WIN32
    ApiRequestHandleSet handles;
    if (!BeginApiRequest(
            config, body, 90000u, handles, outError)) {
        return false;
    }

    std::string response;
    char buffer[4096];
    DWORD bytesRead = 0u;
    while (WinHttpReadData(
               handles.request, buffer,
               static_cast<DWORD>(sizeof(buffer)), &bytesRead) &&
           bytesRead > 0u) {
        response.append(buffer, bytesRead);
    }
    DWORD statusCode = 0u;
    DWORD statusSize = sizeof(statusCode);
    (void)WinHttpQueryHeaders(
        handles.request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX);

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
    outRaw = std::move(response);
    return true;
#else
    (void)config;
    (void)body;
    outError = "仅支持 Windows 编辑器宿主";
    return false;
#endif
}

bool RunApiRequestRawStreaming(
    const AiChatConfig& config,
    const std::string& body,
    const std::function<bool(const std::string& chunk)>& onChunk,
    std::string& outError) {
    outError.clear();
#ifdef _WIN32
    ApiRequestHandleSet handles;
    if (!BeginApiRequest(
            config, body, 20000u, handles, outError)) {
        return false;
    }

    char buffer[4096];
    const auto started = std::chrono::steady_clock::now();
    const auto deadline =
        started + std::chrono::seconds(90);
    while (std::chrono::steady_clock::now() < deadline) {
        DWORD bytesRead = 0u;
        if (!WinHttpReadData(
                handles.request, buffer,
                static_cast<DWORD>(sizeof(buffer)), &bytesRead)) {
            const DWORD error = GetLastError();
            if (error == ERROR_WINHTTP_TIMEOUT ||
                error == ERROR_WINHTTP_OPERATION_CANCELLED) {
                outError = "流式响应超时（20 秒无数据）";
            } else {
                outError = "WinHttpReadData 失败 (error " +
                    std::to_string(error) + ")";
            }
            return false;
        }
        if (bytesRead == 0u) {
            break;
        }
        if (onChunk &&
            !onChunk(std::string(buffer, bytesRead))) {
            break;
        }
    }
    DWORD statusCode = 0u;
    DWORD statusSize = sizeof(statusCode);
    (void)WinHttpQueryHeaders(
        handles.request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusSize,
        WINHTTP_NO_HEADER_INDEX);
    if (statusCode >= 400u) {
        outError = "HTTP " + std::to_string(statusCode) + " 响应错误";
        return false;
    }
    return true;
#else
    (void)config;
    (void)body;
    (void)onChunk;
    outError = "仅支持 Windows 编辑器宿主";
    return false;
#endif
}

bool RunApiRequest(
    const AiChatConfig& config,
    const std::string& body,
    std::string& outContent,
    std::string& outError) {
    std::string raw;
    if (!RunApiRequestRaw(config, body, raw, outError)) {
        return false;
    }
    outContent = ExtractJsonContent(raw);
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
    std::string& outError) {
    outFull.clear();
    outError.clear();
    if (!config.valid) {
        outError = "未配置 API Key（DASHSCOPE_API_KEY）";
        return false;
    }
    const std::string body =
        BuildRequestBody(config, messages, true);
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
    const bool streamOk = RunApiRequestRawStreaming(
        config, body, onChunk, streamError);
    // Flush any trailing partial SSE line (for example when the stream times
    // out before a final newline) so a complete action is not silently lost.
    (void)onChunk(std::string{});
    if (userAbort) {
        outError = "用户中止";
        return false;
    }
    if (!streamOk && full.empty()) {
        outError = streamError;
        return false;
    }
    if (!sawDone && !streamOk && !full.empty()) {
        outError = "（流式响应超时，内容可能不完整）";
        outFull = std::move(full);
        return true;
    }
    if (!sawDone && full.empty()) {
        outError = "API 没有返回可识别的流式内容";
        return false;
    }
    outFull = std::move(full);
    return !outFull.empty();
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
