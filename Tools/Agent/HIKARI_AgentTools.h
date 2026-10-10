#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <json.hpp>

#include "HIKARI_AgentClient.h"

// hikari.exe の命令行と MCP が共有する道具の表。どちらも同じ要求を編集器の本地桥へ送る。
namespace HIKARI::AGENT_CLI {

struct AgentToolOutcome {
    // 本地桥の結果（{ok, code, message, data, ...}）。
    nlohmann::json result = nlohmann::json::object();
    // 結果に含まれる画像（截图）。MCP は画像として返し、命令行はパスを出す。
    std::vector<std::filesystem::path> images{};
    // MCP で画像を元の大きさのまま返す（既定は kModelImageLongSide へ縮める）。
    bool fullSizeImages = false;
};

struct AgentTool {
    const char* name;
    const char* description;
    nlohmann::json inputSchema;
    std::optional<AgentToolOutcome> (*invoke)(AgentClient& client, const nlohmann::json& arguments,
                                              std::string& outError);
};

const std::vector<AgentTool>& AgentTools();
const AgentTool* FindAgentTool(std::string_view name);
// 道具の引数を宣言どおりに整える。知らない引数は黙って捨てずに断り、対象・配列の引数が JSON 文字列で来たら解く
// （模型はよく文字列にして渡す）。問題があれば理由を返す。MCP と命令行はこれを通してから invoke を呼ぶ。
std::optional<std::string> NormalizeAgentToolArguments(const AgentTool& tool, nlohmann::json& arguments);

// MCP の initialize が返す、智能体向けの使い方。
const char* AgentUsageInstructions();

} // namespace HIKARI::AGENT_CLI
