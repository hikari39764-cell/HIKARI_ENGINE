#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <json.hpp>

namespace HIKARI::EDITOR::AGENT {

// 外部の智能体向けの本地桥。127.0.0.1 の空きポートだけで待ち受け、起動ごとの乱数令牌（Bearer）で認証する。
// 接続ごとに一つのスレッドで処理するので、承認待ちの要求が他の要求を止めない。
// 要求本文（{"operation": 操作 ID, "parameters": {...}}）は executor が受け取り、結果の JSON を返す。executor は任意のスレッドから
// 呼ばれ、Stop の前には呼び出し側が待っている要求を終わらせる（終わらない executor があると Stop が待ち続ける）。
class AgentBridge {
public:
    using Executor = std::function<nlohmann::json(std::string_view)>;

    AgentBridge();
    ~AgentBridge();

    AgentBridge(const AgentBridge&) = delete;
    AgentBridge& operator=(const AgentBridge&) = delete;

    // port 0 は空きポート。
    bool Start(Executor executor, std::string& outError, uint16_t port = 0u);
    void Stop();
    // 接続情報を書く。前に書いたファイルが別の場所なら消してから書く。
    bool PublishConnection(const std::filesystem::path& path, const std::string& projectRootUtf8,
                           std::string& outError);

    bool IsRunning() const noexcept { return running_.load(); }
    std::string GetEndpoint() const;
    std::filesystem::path GetConnectionPath() const;

private:
    struct Connection {
        std::thread thread{};
        std::shared_ptr<std::atomic<bool>> done{};
    };

    void AcceptLoop();
    void Serve(uintptr_t clientSocket) const;
    void ReapFinishedConnections();
    void UnpublishConnection();

    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<size_t> activeConnections_{0};
    std::thread acceptThread_{};
    mutable std::mutex stateMutex_{};
    std::vector<Connection> connections_{};
    uintptr_t listener_ = 0u;
    uint16_t port_ = 0u;
    std::string token_{};
    Executor executor_{};
    std::filesystem::path connectionPath_{};
};

} // namespace HIKARI::EDITOR::AGENT
