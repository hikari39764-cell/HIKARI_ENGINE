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

// MCP の initialize が返す、智能体向けの使い方。
const char* AgentUsageInstructions();

} // namespace HIKARI::AGENT_CLI
