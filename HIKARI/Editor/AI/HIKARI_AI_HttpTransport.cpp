#include "HIKARI_AI_HttpTransport.h"
#include <array>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace HIKARI::EDITOR::AI_ASSISTANT {
#ifdef _WIN32
namespace {
std::wstring Wide(const std::string& text) {
    const int length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(length), L'\0');
    if (length)
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}
struct ResponseState {
    std::mutex mutex;
    std::condition_variable changed;
    DWORD completed = 0, error = 0, readBytes = 0;
    std::array<char, 8192> bytes{};
    std::string body;
};
using CallbackOwner = std::shared_ptr<ResponseState>;
void CALLBACK OnStatus(HINTERNET, DWORD_PTR context, DWORD status, void* info, DWORD length) {
    auto* owner = reinterpret_cast<CallbackOwner*>(context);
    if (!owner)
        return;
    auto state = *owner;
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        delete owner;
        return;
    }
    std::lock_guard<std::mutex> lock(state->mutex);
    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR)
        state->error = static_cast<WINHTTP_ASYNC_RESULT*>(info)->dwError;
    else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE ||
             status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE || status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
        state->completed = status;
        if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE)
            state->readBytes = length;
    }
    state->changed.notify_all();
}
struct Handles {
    HINTERNET session = nullptr, connection = nullptr, request = nullptr;
    ~Handles() {
        // async API が戻った後だけ close する。callback の所有権は HANDLE_CLOSING まで残す。
        if (request)
            WinHttpCloseHandle(request);
        if (connection)
            WinHttpCloseHandle(connection);
        if (session)
            WinHttpCloseHandle(session);
    }
};
} // namespace
#endif
bool RunAiHttpStream(const AiChatConfig& config, const std::string& body,
                     const std::function<bool(const std::string&)>& onChunk, std::string& error) {
    error.clear();
#ifdef _WIN32
    if (!config.valid) {
        error = "未配置 API Key。";
        return false;
    }
    const auto canceled = [&] { return config.shouldCancel && config.shouldCancel(); };
    if (canceled()) {
        error = "用户已停止任务。";
        return false;
    }
    auto url = Wide(config.baseUrl);
    while (!url.empty() && url.back() == L'/')
        url.pop_back();
    if (!url.ends_with(L"/chat/completions"))
        url += L"/chat/completions";
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) ||
        (parts.nScheme != INTERNET_SCHEME_HTTPS && parts.nScheme != INTERNET_SCHEME_HTTP)) {
        error = "API 地址必须是有效的 HTTP 或 HTTPS 地址。";
        return false;
    }
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    Handles handles;
    handles.session = WinHttpOpen(L"HIKARI Assistant", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                  WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (!handles.session) {
        error = "无法初始化 HTTP。";
        return false;
    }
    WinHttpSetTimeouts(handles.session, 5000, 5000, 30000, 90000);
    handles.connection = WinHttpConnect(handles.session, host.c_str(), parts.nPort, 0);
    if (!handles.connection) {
        error = "无法连接 API。";
        return false;
    }
    handles.request = WinHttpOpenRequest(handles.connection, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!handles.request) {
        error = "无法创建 API 请求。";
        return false;
    }
    auto state = std::make_shared<ResponseState>();
    state->body = body;
    auto* owner = new CallbackOwner(state);
    const DWORD_PTR context = reinterpret_cast<DWORD_PTR>(owner);
    if (!WinHttpSetOption(handles.request, WINHTTP_OPTION_CONTEXT_VALUE, const_cast<DWORD_PTR*>(&context),
                          sizeof(context))) {
        delete owner;
        error = "无法设置请求上下文。";
        return false;
    }
    if (WinHttpSetStatusCallback(handles.request, OnStatus,
                                 WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES,
                                 0) == WINHTTP_INVALID_STATUS_CALLBACK) {
        delete owner;
        error = "无法设置异步请求回调。";
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    const auto wait = [&](DWORD expected) {
        std::unique_lock<std::mutex> lock(state->mutex);
        while (!state->error && state->completed != expected) {
            if (canceled()) {
                error = "用户已停止任务。";
                return false;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                error = "API 请求超时。";
                return false;
            }
            state->changed.wait_for(lock, std::chrono::milliseconds(50));
        }
        if (state->error) {
            error = "HTTP 传输失败 (" + std::to_string(state->error) + ")";
            return false;
        }
        state->completed = 0;
        return true;
    };
    const auto started = [&](BOOL success) {
        if (success || GetLastError() == ERROR_IO_PENDING)
            return true;
        error = "HTTP 请求失败 (" + std::to_string(GetLastError()) + ")";
        return false;
    };
    const std::wstring headers =
        L"Content-Type: application/json\r\nAuthorization: Bearer " + Wide(config.apiKey) + L"\r\n";
    if (!started(WinHttpSendRequest(handles.request, headers.c_str(), static_cast<DWORD>(-1), state->body.data(),
                                    static_cast<DWORD>(state->body.size()), static_cast<DWORD>(state->body.size()),
                                    context)) ||
        !wait(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE))
        return false;
    if (!started(WinHttpReceiveResponse(handles.request, nullptr)) || !wait(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE))
        return false;
    DWORD status = 0, length = sizeof(status);
    if (!WinHttpQueryHeaders(handles.request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX)) {
        error = "无法读取 HTTP 状态。";
        return false;
    }
    std::string errorBody;
    for (;;) {
        if (canceled()) {
            error = "用户已停止任务。";
            return false;
        }
        if (!started(WinHttpReadData(handles.request, state->bytes.data(), static_cast<DWORD>(state->bytes.size()),
                                     nullptr)) ||
            !wait(WINHTTP_CALLBACK_STATUS_READ_COMPLETE))
            return false;
        const auto count = state->readBytes;
        if (!count)
            break;
        const std::string chunk(state->bytes.data(), count);
        if (status >= 400) {
            errorBody += chunk;
            if (errorBody.size() >= 8192)
                break;
        } else if (onChunk && !onChunk(chunk))
            break;
    }
    if (status >= 400) {
        const auto parsed = nlohmann::json::parse(errorBody, nullptr, false);
        error = "HTTP " + std::to_string(status);
        if (parsed.is_object() && parsed.contains("error") && parsed["error"].is_object() &&
            parsed["error"].contains("message"))
            if (parsed["error"]["message"].is_string())
                error += " · " + parsed["error"]["message"].get<std::string>();
        return false;
    }
    return true;
#else
    (void)config;
    (void)body;
    (void)onChunk;
    error = "当前宿主不支持 AI HTTP 传输。";
    return false;
#endif
}
} // namespace HIKARI::EDITOR::AI_ASSISTANT
