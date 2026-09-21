#include "HIKARI_AI_LocalBridge.h"

#include "Core/Text/HIKARI_Trim.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <string>
#include <utility>

#include <json.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

namespace HIKARI::EDITOR::AI_ASSISTANT {
namespace {

constexpr size_t kMaxRequestBody = 1024u * 1024u;
constexpr size_t kSessionTokenBytes = 32u;

std::string Trim(std::string value) {
    return TEXT::TrimCopy(std::move(value));
}

bool InitializeWinsock(std::string& outError) {
#ifdef _WIN32
    WSADATA data{};
    const int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result != 0) {
        outError =
            "WSAStartup 失败 (error " + std::to_string(result) + ")";
        return false;
    }
    return true;
#else
    (void)outError;
    return false;
#endif
}

bool GenerateSessionToken(
    std::string& outToken,
    std::string& outError) {
#ifdef _WIN32
    std::array<unsigned char, kSessionTokenBytes> bytes{};
    const NTSTATUS status = BCryptGenRandom(
        nullptr,
        bytes.data(),
        static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (!BCRYPT_SUCCESS(status)) {
        outError = "生成本地桥会话令牌失败";
        return false;
    }
    constexpr char digits[] = "0123456789abcdef";
    outToken.resize(bytes.size() * 2u);
    for (size_t index = 0u; index < bytes.size(); ++index) {
        outToken[index * 2u] = digits[bytes[index] >> 4u];
        outToken[index * 2u + 1u] = digits[bytes[index] & 0x0fu];
    }
    return true;
#else
    (void)outToken;
    outError = "本地桥仅支持 Windows 编辑器宿主";
    return false;
#endif
}

bool ConstantTimeEquals(
    std::string_view lhs,
    std::string_view rhs) noexcept {
    const size_t maximum = std::max(lhs.size(), rhs.size());
    size_t difference = lhs.size() ^ rhs.size();
    for (size_t index = 0u; index < maximum; ++index) {
        const unsigned char left = index < lhs.size()
            ? static_cast<unsigned char>(lhs[index])
            : 0u;
        const unsigned char right = index < rhs.size()
            ? static_cast<unsigned char>(rhs[index])
            : 0u;
        difference |= static_cast<size_t>(left ^ right);
    }
    return difference == 0u;
}

std::string BuildCapabilitiesJson(
    const std::vector<AiCapability>& sourceCapabilities) {
    nlohmann::json capabilitiesArray = nlohmann::json::array();
    for (const AiCapability& capability : sourceCapabilities) {
        capabilitiesArray.push_back(DescribeAiCapability(capability));
    }
    return nlohmann::json{
        { "capabilities", capabilitiesArray }
    }.dump();
}

std::string BuildResultJson(const AiRequestResult& result) {
    return nlohmann::json{
        { "ok", result.ok },
        { "code", result.code },
        { "message", result.message },
        { "data", result.data },
        { "details", result.details },
        { "hints", result.hints },
    }.dump();
}

std::string HttpStatusText(int statusCode) {
    switch (statusCode) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default: return "Error";
    }
}

} // namespace

AiLocalBridge::AiLocalBridge() = default;

AiLocalBridge::~AiLocalBridge() {
    Stop();
}

bool AiLocalBridge::Start(
    uint16_t port,
    Executor executor,
    std::string& outError) {
    if (running_.load()) {
        outError = "本地桥已在运行";
        return false;
    }
    std::string sessionToken;
    if (!GenerateSessionToken(sessionToken, outError)) {
        return false;
    }
    if (!InitializeWinsock(outError)) {
        return false;
    }
#ifdef _WIN32
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        outError = "创建监听套接字失败";
        WSACleanup();
        return false;
    }
    int reuse = 1;
    (void)setsockopt(
        listener, SOL_SOCKET, SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (bind(
            listener,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) == SOCKET_ERROR) {
        closesocket(listener);
        WSACleanup();
        outError = "绑定 127.0.0.1:" +
            std::to_string(port) + " 失败（端口可能被占用）";
        return false;
    }
    if (listen(listener, 4) == SOCKET_ERROR) {
        closesocket(listener);
        WSACleanup();
        outError = "监听失败";
        return false;
    }

    sockaddr_in bound{};
    int boundSize = sizeof(bound);
    uint16_t actualPort = port;
    if (getsockname(
            listener,
            reinterpret_cast<sockaddr*>(&bound),
            &boundSize) == 0) {
        actualPort = ntohs(bound.sin_port);
    }
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        listener_ = static_cast<uintptr_t>(listener);
        port_ = actualPort;
        endpoint_ =
            "http://127.0.0.1:" + std::to_string(actualPort);
        sessionToken_ = std::move(sessionToken);
        executor_ = std::move(executor);
    }
    stopRequested_.store(false);
    running_.store(true);
    try {
        thread_ = std::thread(&AiLocalBridge::AcceptLoop, this);
    } catch (...) {
        running_.store(false);
        closesocket(listener);
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            listener_ = 0u;
            port_ = 0u;
            endpoint_.clear();
            sessionToken_.clear();
            executor_ = {};
        }
        WSACleanup();
        outError = "启动桥接线程失败";
        return false;
    }
    return true;
#else
    (void)port;
    (void)executor;
    outError = "本地桥仅支持 Windows 编辑器宿主";
    return false;
#endif
}

void AiLocalBridge::Stop() {
    const bool wasRunning = running_.exchange(false);
    if (!wasRunning && !thread_.joinable()) {
        return;
    }
    stopRequested_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (listener_ != 0u) {
#ifdef _WIN32
        closesocket(static_cast<SOCKET>(listener_));
#endif
        listener_ = 0u;
    }
    port_ = 0u;
    endpoint_.clear();
    sessionToken_.clear();
    executor_ = {};
#ifdef _WIN32
    WSACleanup();
#endif
}

void AiLocalBridge::SetCapabilityProvider(
    CapabilityProvider callback) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    capabilityProvider_ = std::move(callback);
}

bool AiLocalBridge::IsRunning() const {
    return running_.load();
}

uint16_t AiLocalBridge::GetPort() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return port_;
}

std::string AiLocalBridge::GetEndpoint() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return endpoint_;
}

std::string AiLocalBridge::GetSessionToken() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return sessionToken_;
}

void AiLocalBridge::AcceptLoop() {
#ifdef _WIN32
    SOCKET listener = INVALID_SOCKET;
    Executor executor;
    CapabilityProvider capabilityProvider;
    std::string sessionToken;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        listener = static_cast<SOCKET>(listener_);
        executor = executor_;
        capabilityProvider = capabilityProvider_;
        sessionToken = sessionToken_;
    }
    while (!stopRequested_.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listener, &readSet);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 200000;
        const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) {
            break;
        }
        if (ready == 0) {
            continue;
        }
        sockaddr_in clientAddress{};
        int clientSize = sizeof(clientAddress);
        const SOCKET client = accept(
            listener,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &clientSize);
        if (client == INVALID_SOCKET) {
            continue;
        }
        HandleConnection(
            static_cast<uintptr_t>(client),
            executor,
            capabilityProvider,
            sessionToken);
    }
#endif
}

void AiLocalBridge::HandleConnection(
    uintptr_t clientSocket,
    const Executor& executor,
    const CapabilityProvider& capabilityProvider,
    const std::string& sessionToken) {
#ifdef _WIN32
    const SOCKET client = static_cast<SOCKET>(clientSocket);
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(10);
    const auto readMore = [&](std::string& target, size_t needed) {
        while (target.size() < needed &&
               std::chrono::steady_clock::now() < deadline) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(client, &readSet);
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 500000;
            const int ready =
                select(0, &readSet, nullptr, nullptr, &timeout);
            if (ready == SOCKET_ERROR) {
                return false;
            }
            if (ready == 0) {
                continue;
            }
            char buffer[4096];
            const int received =
                recv(client, buffer, sizeof(buffer), 0);
            if (received <= 0) {
                return false;
            }
            target.append(buffer, static_cast<size_t>(received));
        }
        return target.size() >= needed;
    };

    std::string request;
    if (!readMore(request, 4u) ||
        request.find("\r\n\r\n") == std::string::npos) {
        closesocket(client);
        return;
    }
    const auto headerEnd = request.find("\r\n\r\n");
    const std::string head = request.substr(0u, headerEnd);
    std::string body = request.substr(headerEnd + 4u);

    const auto firstLineEnd = head.find("\r\n");
    const std::string requestLine =
        head.substr(0u, firstLineEnd);
    const auto firstSpace = requestLine.find(' ');
    const auto secondSpace =
        requestLine.find(' ', firstSpace + 1u);
    if (firstSpace == std::string::npos ||
        secondSpace == std::string::npos) {
        closesocket(client);
        return;
    }
    const std::string method =
        requestLine.substr(0u, firstSpace);
    const std::string path = requestLine.substr(
        firstSpace + 1u, secondSpace - firstSpace - 1u);

    size_t contentLength = 0u;
    const auto lengthMarker = head.find("Content-Length:");
    if (lengthMarker != std::string::npos) {
        const auto valueStart = head.find(':', lengthMarker) + 1u;
        const auto valueEnd = head.find("\r\n", valueStart);
        const std::string value = Trim(
            head.substr(valueStart, valueEnd - valueStart));
        try {
            contentLength = static_cast<size_t>(std::stoull(value));
        } catch (...) {
            contentLength = 0u;
        }
    }
    if (contentLength > kMaxRequestBody) {
        const std::string response =
            "HTTP/1.1 413 Payload Too Large\r\n"
            "Connection: close\r\n\r\n";
        (void)send(client, response.data(),
                   static_cast<int>(response.size()), 0);
        closesocket(client);
        return;
    }
    if (body.size() < contentLength &&
        !readMore(body, contentLength)) {
        closesocket(client);
        return;
    }
    body = body.substr(0u, contentLength);

    const bool requiresAuthorization =
        !(method == "GET" && (path == "/health" || path == "/"));
    if (requiresAuthorization) {
        bool authorized = false;
        size_t cursor = 0u;
        while (cursor < head.size()) {
            const auto lineEnd = head.find("\r\n", cursor);
            const std::string line = head.substr(
                cursor,
                (lineEnd == std::string::npos ? head.size() : lineEnd) -
                    cursor);
            cursor = lineEnd == std::string::npos
                ? head.size()
                : lineEnd + 2u;
            if (Trim(line).rfind("X-HIKARI-AI-Token:", 0u) == 0u) {
                const std::string value = Trim(line.substr(
                    std::string("X-HIKARI-AI-Token:").size()));
                authorized = ConstantTimeEquals(value, sessionToken);
                break;
            }
        }
        if (!authorized) {
            const std::string response =
                "HTTP/1.1 401 Unauthorized\r\n"
                "Content-Type: application/json\r\n"
                "Connection: close\r\n\r\n"
                "{\"ok\":false,\"message\":\"无效的桥接令牌\"}";
            (void)send(client, response.data(),
                       static_cast<int>(response.size()), 0);
            closesocket(client);
            return;
        }
    }

    int statusCode = 200;
    std::string payload;
    if (method == "GET" &&
        (path == "/health" || path == "/")) {
        payload = nlohmann::json{ { "ok", true } }.dump();
    } else if (method == "GET" && path == "/capabilities") {
        payload = BuildCapabilitiesJson(
            capabilityProvider
                ? capabilityProvider()
                : BuiltInCapabilities());
    } else if (method == "POST" && path == "/action") {
        if (body.empty()) {
            statusCode = 400;
            payload = nlohmann::json{
                { "ok", false },
                { "message", "空请求体" },
            }.dump();
        } else {
            try {
                payload = BuildResultJson(executor(body));
            } catch (...) {
                statusCode = 500;
                payload = nlohmann::json{
                    { "ok", false },
                    { "message", "（内部错误）" },
                }.dump();
            }
        }
    } else {
        statusCode = 404;
        payload = nlohmann::json{
            { "ok", false },
            { "message", "未找到路由：" + method + " " + path },
        }.dump();
    }

    std::string response =
        "HTTP/1.1 " + std::to_string(statusCode) + " " +
        HttpStatusText(statusCode) + "\r\n";
    response +=
        "Content-Type: application/json\r\n"
        "Connection: close\r\n";
    response += "Content-Length: " +
        std::to_string(payload.size()) + "\r\n\r\n";
    response += payload;
    size_t sent = 0u;
    while (sent < response.size()) {
        const int result = send(
            client,
            response.data() + sent,
            static_cast<int>(response.size() - sent), 0);
        if (result <= 0) {
            break;
        }
        sent += static_cast<size_t>(result);
    }
    closesocket(client);
#endif
}

} // namespace HIKARI::EDITOR::AI_ASSISTANT
