#include "HIKARI_AgentClient.h"

#include <thread>
#include <utility>

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

namespace HIKARI::AGENT_CLI {
namespace {

constexpr auto kPollInterval = std::chrono::milliseconds(200);
constexpr auto kReconnectInterval = std::chrono::milliseconds(500);

bool IsProjectFolder(const std::filesystem::path& folder) {
    std::error_code error;
    if (std::filesystem::is_regular_file(folder / "Project.hikari", error))
        return true;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        if (entry.is_regular_file(error) && entry.path().extension() == ".hikari")
            return true;
    }
    return false;
}

struct WinsockSession {
    bool ok = false;
    WinsockSession() {
        WSADATA data{};
        ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockSession() {
        if (ok)
            WSACleanup();
    }
};

} // namespace

std::optional<std::filesystem::path> FindProjectRoot(const std::filesystem::path& start) {
    std::error_code error;
    std::filesystem::path current = std::filesystem::absolute(start, error);
    while (!error && !current.empty()) {
        if (IsProjectFolder(current))
            return current;
        const std::filesystem::path parent = current.parent_path();
        if (parent == current)
            break;
        current = parent;
    }
    return std::nullopt;
}

AgentClient::AgentClient(AgentClientOptions options) : options_(std::move(options)) {
    connectionPath_ = EDITOR::AGENT::FindAgentConnectionPath(options_.projectRoot);
}

bool AgentClient::Refresh(std::string& outError) {
    connection_ = EDITOR::AGENT::ReadAgentConnection(connectionPath_, outError);
    return connection_.has_value();
}

int AgentClient::Post(const std::string& body, std::string& outResponse, std::string& outError) const {
    WinsockSession winsock;
    if (!winsock.ok) {
        outError = "WSAStartup 失败";
        return 0;
    }
    const SOCKET client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCKET) {
        outError = "无法创建套接字";
        return 0;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(connection_->port);
    if (connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        closesocket(client);
        outError = "无法连接编辑器（127.0.0.1:" + std::to_string(connection_->port) + "）。编辑器是否还在运行？";
        return 0;
    }
    std::string request =
        "POST /v2/operation HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(connection_->port) +
        "\r\nAuthorization: Bearer " + connection_->token +
        "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\n\r\n" + body;
    for (size_t sent = 0u; sent < request.size();) {
        const int result = send(client, request.data() + sent, static_cast<int>(request.size() - sent), 0);
        if (result <= 0) {
            closesocket(client);
            outError = "发送请求失败";
            return 0;
        }
        sent += static_cast<size_t>(result);
    }
    const auto deadline = std::chrono::steady_clock::now() + options_.requestTimeout;
    std::string response;
    while (true) {
        if (std::chrono::steady_clock::now() >= deadline) {
            closesocket(client);
            outError = "等待编辑器回应超时（可能在等待用户审批）";
            return 0;
        }
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(client, &readSet);
        timeval timeout{0, 200000};
        const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready == SOCKET_ERROR)
            break;
        if (ready == 0)
            continue;
        char chunk[8192];
        const int received = recv(client, chunk, sizeof(chunk), 0);
        if (received <= 0)
            break;
        response.append(chunk, static_cast<size_t>(received));
    }
    closesocket(client);
    const size_t headerEnd = response.find("\r\n\r\n");
    const size_t firstSpace = response.find(' ');
    if (headerEnd == std::string::npos || firstSpace == std::string::npos || firstSpace > headerEnd) {
        outError = "编辑器的回应不完整";
        return 0;
    }
    outResponse = response.substr(headerEnd + 4u);
    try {
        return std::stoi(response.substr(firstSpace + 1u, 3u));
    } catch (...) {
        outError = "编辑器的回应格式不正确";
        return 0;
    }
}

std::optional<nlohmann::json> AgentClient::Call(const std::string& operation, const nlohmann::json& parameters,
                                                std::string& outError) {
    const std::string body = nlohmann::json{{"operation", operation}, {"parameters", parameters}}.dump();
    const auto giveUp = std::chrono::steady_clock::now() + options_.reconnectWait;
    bool refreshed = false;
    while (true) {
        if (!connection_ && !Refresh(outError)) {
            if (std::chrono::steady_clock::now() >= giveUp)
                return std::nullopt;
            std::this_thread::sleep_for(kReconnectInterval);
            continue;
        }
        std::string response;
        const int status = Post(body, response, outError);
        // 編集器が再起動していれば、連接文件の新しいポートと令牌で一度やり直す。
        if (status == 0 || status == 401) {
            if (status == 401 && refreshed) {
                outError = "令牌被拒绝，请确认连接的是同一个编辑器。";
                return std::nullopt;
            }
            connection_.reset();
            refreshed = status == 401;
            if (status == 0 && std::chrono::steady_clock::now() >= giveUp)
                return std::nullopt;
            if (status == 0)
                std::this_thread::sleep_for(kReconnectInterval);
            continue;
        }
        nlohmann::json result = nlohmann::json::parse(response, nullptr, false);
        if (result.is_discarded() || !result.is_object()) {
            outError = "编辑器返回的 JSON 无法解析（HTTP " + std::to_string(status) + "）";
            return std::nullopt;
        }
        return result;
    }
}

std::optional<nlohmann::json> AgentClient::CallFollowingPoll(const std::string& operation,
                                                             const nlohmann::json& parameters,
                                                             std::chrono::seconds wait, std::string& outError) {
    std::optional<nlohmann::json> result = Call(operation, parameters, outError);
    const auto deadline = std::chrono::steady_clock::now() + wait;
    while (result && result->value("ok", false) && result->contains("data") && (*result)["data"].is_object() &&
           (*result)["data"].value("pending", false) && std::chrono::steady_clock::now() < deadline) {
        const nlohmann::json poll = (*result)["data"].value("poll", nlohmann::json{});
        if (!poll.is_object() || !poll.contains("operation") || !poll["operation"].is_string())
            break;
        std::this_thread::sleep_for(kPollInterval);
        result =
            Call(poll["operation"].get<std::string>(), poll.value("parameters", nlohmann::json::object()), outError);
    }
    return result;
}

} // namespace HIKARI::AGENT_CLI
