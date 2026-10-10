#include "Editor/Agent/HIKARI_AgentBridge.h"

#include "Editor/Agent/HIKARI_AgentConnection.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <iterator>
#include <optional>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

namespace HIKARI::EDITOR::AGENT {
namespace {

constexpr size_t kMaximumHeaderBytes = 16u * 1024u;
constexpr size_t kMaximumRequestBody = 1024u * 1024u;
constexpr size_t kMaximumConnections = 16u;
constexpr size_t kTokenBytes = 32u;
constexpr auto kRequestReadTimeout = std::chrono::seconds(10);

nlohmann::json Failure(const std::string& code, const std::string& message) {
    return {{"ok", false}, {"code", code}, {"message", message}};
}

bool ConstantTimeEquals(std::string_view lhs, std::string_view rhs) noexcept {
    const size_t maximum = (std::max)(lhs.size(), rhs.size());
    size_t difference = lhs.size() ^ rhs.size();
    for (size_t index = 0u; index < maximum; ++index) {
        const unsigned char left = index < lhs.size() ? static_cast<unsigned char>(lhs[index]) : 0u;
        const unsigned char right = index < rhs.size() ? static_cast<unsigned char>(rhs[index]) : 0u;
        difference |= static_cast<size_t>(left ^ right);
    }
    return difference == 0u;
}

std::string_view TrimView(std::string_view text) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

// ヘッダー名は大文字小文字を区別しない。
std::optional<std::string_view> HeaderValue(std::string_view head, std::string_view name) {
    size_t cursor = head.find("\r\n");
    while (cursor != std::string_view::npos && cursor + 2u < head.size()) {
        const size_t begin = cursor + 2u;
        const size_t end = head.find("\r\n", begin);
        const std::string_view line = head.substr(begin, (end == std::string_view::npos ? head.size() : end) - begin);
        const size_t colon = line.find(':');
        if (colon == name.size() && std::equal(name.begin(), name.end(), line.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            })) {
            return TrimView(line.substr(colon + 1u));
        }
        cursor = end;
    }
    return std::nullopt;
}

const char* StatusText(int status) noexcept {
    switch (status) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 404: return "Not Found";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    default: return "Error";
    }
}

#ifdef _WIN32
bool GenerateToken(std::string& outToken, std::string& outError) {
    std::array<unsigned char, kTokenBytes> bytes{};
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        outError = "生成本地桥令牌失败";
        return false;
    }
    constexpr char digits[] = "0123456789abcdef";
    outToken.resize(bytes.size() * 2u);
    for (size_t index = 0u; index < bytes.size(); ++index) {
        outToken[index * 2u] = digits[bytes[index] >> 4u];
        outToken[index * 2u + 1u] = digits[bytes[index] & 0x0fu];
    }
    return true;
}

void SendResponse(SOCKET client, int status, const nlohmann::json& payload) {
    const std::string body = payload.dump();
    std::string response = "HTTP/1.1 " + std::to_string(status) + " " + StatusText(status) + "\r\n";
    response += "Content-Type: application/json; charset=utf-8\r\nConnection: close\r\n";
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
    response += body;
    size_t sent = 0u;
    while (sent < response.size()) {
        const int result = send(client, response.data() + sent, static_cast<int>(response.size() - sent), 0);
        if (result <= 0) {
            break;
        }
        sent += static_cast<size_t>(result);
    }
}

// deadline までに until が真になるまで受け取る。接続が閉じたか時間切れなら false。
template <class Until>
bool ReceiveUntil(SOCKET client, std::string& buffer, std::chrono::steady_clock::time_point deadline, size_t limit,
                  Until until) {
    while (!until(buffer)) {
        if (buffer.size() > limit || std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(client, &readSet);
        timeval timeout{0, 200000};
        const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR) {
            return false;
        }
        if (ready == 0) {
            continue;
        }
        char chunk[4096];
        const int received = recv(client, chunk, sizeof(chunk), 0);
        if (received <= 0) {
            return false;
        }
        buffer.append(chunk, static_cast<size_t>(received));
    }
    return true;
}
#endif

} // namespace

AgentBridge::AgentBridge() = default;

AgentBridge::~AgentBridge() { Stop(); }

bool AgentBridge::Start(Executor executor, std::string& outError, uint16_t port) {
    if (running_.load()) {
        outError = "本地桥已在运行";
        return false;
    }
#ifdef _WIN32
    std::string token;
    if (!GenerateToken(token, outError)) {
        return false;
    }
    WSADATA data{};
    if (const int result = WSAStartup(MAKEWORD(2, 2), &data); result != 0) {
        outError = "WSAStartup 失败 (error " + std::to_string(result) + ")";
        return false;
    }
    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        WSACleanup();
        outError = "创建监听套接字失败";
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    sockaddr_in bound{};
    int boundSize = sizeof(bound);
    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
        listen(listener, 8) == SOCKET_ERROR ||
        getsockname(listener, reinterpret_cast<sockaddr*>(&bound), &boundSize) == SOCKET_ERROR) {
        closesocket(listener);
        WSACleanup();
        outError = "监听 127.0.0.1:" + std::to_string(port) + " 失败";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        listener_ = static_cast<uintptr_t>(listener);
        port_ = ntohs(bound.sin_port);
        token_ = std::move(token);
        executor_ = std::move(executor);
    }
    stopRequested_.store(false);
    running_.store(true);
    try {
        acceptThread_ = std::thread(&AgentBridge::AcceptLoop, this);
    } catch (...) {
        running_.store(false);
        closesocket(listener);
        WSACleanup();
        std::lock_guard<std::mutex> lock(stateMutex_);
        listener_ = 0u;
        port_ = 0u;
        token_.clear();
        executor_ = {};
        outError = "启动本地桥线程失败";
        return false;
    }
    return true;
#else
    (void)executor;
    (void)port;
    outError = "本地桥仅支持 Windows 编辑器宿主";
    return false;
#endif
}

void AgentBridge::Stop() {
    if (!running_.exchange(false) && !acceptThread_.joinable()) {
        return;
    }
    stopRequested_.store(true);
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    std::vector<Connection> connections;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        connections.swap(connections_);
    }
    for (Connection& connection : connections) {
        if (connection.thread.joinable()) {
            connection.thread.join();
        }
    }
    UnpublishConnection();
    std::lock_guard<std::mutex> lock(stateMutex_);
#ifdef _WIN32
    if (listener_ != 0u) {
        closesocket(static_cast<SOCKET>(listener_));
    }
    WSACleanup();
#endif
    listener_ = 0u;
    port_ = 0u;
    token_.clear();
    executor_ = {};
}

bool AgentBridge::PublishConnection(const std::filesystem::path& path, const std::string& projectRootUtf8,
                                    std::string& outError) {
    if (!running_.load()) {
        outError = "本地桥未运行";
        return false;
    }
    AgentConnection connection{};
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        connection.port = port_;
        connection.token = token_;
    }
#ifdef _WIN32
    connection.processId = static_cast<uint32_t>(GetCurrentProcessId());
#endif
    connection.projectRoot = projectRootUtf8;
    if (GetConnectionPath() != path) {
        UnpublishConnection();
    }
    if (!WriteAgentConnection(path, connection, outError)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(stateMutex_);
    connectionPath_ = path;
    return true;
}

void AgentBridge::UnpublishConnection() {
    std::filesystem::path path;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        path = std::exchange(connectionPath_, {});
    }
#ifdef _WIN32
    if (!path.empty()) {
        RemoveAgentConnection(path, static_cast<uint32_t>(GetCurrentProcessId()));
    }
#endif
}

std::string AgentBridge::GetEndpoint() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return port_ != 0u ? "http://127.0.0.1:" + std::to_string(port_) : std::string{};
}

std::filesystem::path AgentBridge::GetConnectionPath() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return connectionPath_;
}

void AgentBridge::ReapFinishedConnections() {
    std::vector<Connection> finished;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        const auto split = std::partition(connections_.begin(), connections_.end(),
                                          [](const Connection& connection) { return !connection.done->load(); });
        std::move(split, connections_.end(), std::back_inserter(finished));
        connections_.erase(split, connections_.end());
    }
    for (Connection& connection : finished) {
        connection.thread.join();
    }
}

void AgentBridge::AcceptLoop() {
#ifdef _WIN32
    SOCKET listener = INVALID_SOCKET;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        listener = static_cast<SOCKET>(listener_);
    }
    while (!stopRequested_.load()) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listener, &readSet);
        timeval timeout{0, 200000};
        const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        ReapFinishedConnections();
        if (ready == SOCKET_ERROR) {
            break;
        }
        if (ready == 0) {
            continue;
        }
        const SOCKET client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            continue;
        }
        if (activeConnections_.load() >= kMaximumConnections) {
            SendResponse(client, 503, Failure("BRIDGE_BUSY", "本地桥的并发连接已满，请稍后重试。"));
            closesocket(client);
            continue;
        }
        Connection connection{};
        connection.done = std::make_shared<std::atomic<bool>>(false);
        activeConnections_.fetch_add(1u);
        try {
            connection.thread = std::thread([this, client, done = connection.done] {
                Serve(static_cast<uintptr_t>(client));
                activeConnections_.fetch_sub(1u);
                done->store(true);
            });
        } catch (...) {
            activeConnections_.fetch_sub(1u);
            closesocket(client);
            continue;
        }
        std::lock_guard<std::mutex> lock(stateMutex_);
        connections_.push_back(std::move(connection));
    }
#endif
}

void AgentBridge::Serve(uintptr_t clientSocket) const {
#ifdef _WIN32
    const SOCKET client = static_cast<SOCKET>(clientSocket);
    std::string token;
    Executor executor;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        token = token_;
        executor = executor_;
    }
    const auto deadline = std::chrono::steady_clock::now() + kRequestReadTimeout;
    std::string request;
    if (!ReceiveUntil(client, request, deadline, kMaximumHeaderBytes,
                      [](const std::string& buffer) { return buffer.find("\r\n\r\n") != std::string::npos; })) {
        closesocket(client);
        return;
    }
    const size_t headerEnd = request.find("\r\n\r\n");
    const std::string head = request.substr(0u, headerEnd);
    std::string body = request.substr(headerEnd + 4u);
    const size_t lineEnd = head.find("\r\n");
    const std::string_view line = std::string_view(head).substr(0u, lineEnd);
    const size_t firstSpace = line.find(' ');
    const size_t secondSpace = firstSpace == std::string_view::npos ? firstSpace : line.find(' ', firstSpace + 1u);
    if (secondSpace == std::string_view::npos) {
        SendResponse(client, 400, Failure("BAD_REQUEST", "请求行格式不正确"));
        closesocket(client);
        return;
    }
    const std::string_view method = line.substr(0u, firstSpace);
    const std::string_view path = line.substr(firstSpace + 1u, secondSpace - firstSpace - 1u);

    if (method == "GET" && path == "/health") {
        SendResponse(client, 200,
                     {{"ok", true}, {"protocol", kAgentProtocolVersion}, {"processId", GetCurrentProcessId()}});
        closesocket(client);
        return;
    }
    const std::optional<std::string_view> authorization = HeaderValue(head, "Authorization");
    constexpr std::string_view kBearer = "Bearer ";
    if (!authorization || !authorization->starts_with(kBearer) ||
        !ConstantTimeEquals(authorization->substr(kBearer.size()), token)) {
        SendResponse(client, 401, Failure("UNAUTHORIZED", "令牌无效。请重新读取编辑器的连接文件。"));
        closesocket(client);
        return;
    }
    if (method != "POST" || path != "/v2/operation") {
        SendResponse(client, 404, Failure("NOT_FOUND", "未找到路由：" + std::string(method) + " " + std::string(path)));
        closesocket(client);
        return;
    }
    size_t contentLength = 0u;
    if (const auto length = HeaderValue(head, "Content-Length")) {
        try {
            contentLength = static_cast<size_t>(std::stoull(std::string(*length)));
        } catch (...) {
            contentLength = kMaximumRequestBody + 1u;
        }
    }
    if (contentLength > kMaximumRequestBody) {
        SendResponse(client, 413, Failure("PAYLOAD_TOO_LARGE", "请求体超过 1 MiB"));
        closesocket(client);
        return;
    }
    if (!ReceiveUntil(client, body, deadline, kMaximumRequestBody,
                      [contentLength](const std::string& buffer) { return buffer.size() >= contentLength; })) {
        closesocket(client);
        return;
    }
    body.resize(contentLength);
    if (body.empty()) {
        SendResponse(client, 400, Failure("EMPTY_BODY", "请求体为空。示例：{\"action\":\"editor.state\"}"));
        closesocket(client);
        return;
    }
    try {
        SendResponse(client, 200, executor(body));
    } catch (const std::exception& error) {
        SendResponse(client, 500, Failure("INTERNAL_ERROR", error.what()));
    } catch (...) {
        SendResponse(client, 500, Failure("INTERNAL_ERROR", "内部错误"));
    }
    closesocket(client);
#else
    (void)clientSocket;
#endif
}

} // namespace HIKARI::EDITOR::AGENT
