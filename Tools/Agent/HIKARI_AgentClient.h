#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

#include <json.hpp>

#include "Editor/Agent/HIKARI_AgentConnection.h"

namespace HIKARI::AGENT_CLI {

// start から親へたどり、Project.hikari（または *.hikari）のあるフォルダーを返す。
std::optional<std::filesystem::path> FindProjectRoot(const std::filesystem::path& start);

struct AgentClientOptions {
    std::filesystem::path projectRoot{};
    // 承認待ちも含めて一つの要求を待つ時間。
    std::chrono::seconds requestTimeout{600};
    // 編集器が再起動中（連接文件が無い・古い）の間、つなぎ直しを試す時間。
    std::chrono::seconds reconnectWait{10};
};

// 編集器の本地桥への呼び出し。接続できない・令牌が古い時は連接文件を読み直してつなぎ直す。
class AgentClient {
public:
    explicit AgentClient(AgentClientOptions options);

    // 一つの操作を呼ぶ。本地桥の結果（{ok, code, message, data, details, hints}）。通信できなければ nullopt と理由。
    std::optional<nlohmann::json> Call(const std::string& operation, const nlohmann::json& parameters,
                                       std::string& outError);
    // 結果が data.pending と data.poll（{operation, parameters}）を返す間（截图など）、poll を wait まで呼び続ける。
    std::optional<nlohmann::json> CallFollowingPoll(const std::string& operation, const nlohmann::json& parameters,
                                                    std::chrono::seconds wait, std::string& outError);

private:
    bool Refresh(std::string& outError);
    // HTTP の状態と本文。接続できなければ status 0。
    int Post(const std::string& body, std::string& outResponse, std::string& outError) const;

    AgentClientOptions options_{};
    std::filesystem::path connectionPath_{};
    std::optional<EDITOR::AGENT::AgentConnection> connection_{};
};

} // namespace HIKARI::AGENT_CLI
