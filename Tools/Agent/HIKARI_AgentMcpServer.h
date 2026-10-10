#pragma once

#include "HIKARI_AgentClient.h"

namespace HIKARI::AGENT_CLI {

// 標準入出力の MCP サーバー（JSON-RPC 2.0、改行区切り）。道具は AgentTools() の表。標準入力が閉じたら返る。
int RunMcpServer(AgentClient& client);

} // namespace HIKARI::AGENT_CLI
