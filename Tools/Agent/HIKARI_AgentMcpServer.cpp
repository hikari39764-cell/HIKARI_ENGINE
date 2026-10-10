#include "HIKARI_AgentMcpServer.h"

#include "HIKARI_AgentImage.h"
#include "HIKARI_AgentTools.h"

#include "Core/Text/HIKARI_Base64.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <io.h>

namespace HIKARI::AGENT_CLI {
namespace {

// 縮めても 4 MiB を超える画像はパスだけ返す（文脈を圧迫しないため）。
constexpr std::uintmax_t kMaximumInlineImageBytes = 4u * 1024u * 1024u;
constexpr std::array<std::string_view, 3> kSupportedProtocols = {"2025-06-18", "2025-03-26", "2024-11-05"};

void Send(const nlohmann::json& message) {
    const std::string text = message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
    std::fwrite(text.data(), 1u, text.size(), stdout);
    std::fflush(stdout);
}

void Reply(const nlohmann::json& id, nlohmann::json result) {
    Send({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}});
}

void ReplyError(const nlohmann::json& id, int code, const std::string& message) {
    Send({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}});
}

nlohmann::json ToolList() {
    nlohmann::json tools = nlohmann::json::array();
    for (const AgentTool& tool : AgentTools())
        tools.push_back({{"name", tool.name}, {"description", tool.description}, {"inputSchema", tool.inputSchema}});
    return {{"tools", std::move(tools)}};
}

nlohmann::json CallTool(AgentClient& client, const nlohmann::json& params) {
    const std::string name = params.value("name", std::string{});
    const AgentTool* tool = FindAgentTool(name);
    if (tool == nullptr)
        return {{"content", {{{"type", "text"}, {"text", "Unknown tool: " + name}}}}, {"isError", true}};
    nlohmann::json arguments = params.value("arguments", nlohmann::json::object());
    if (!arguments.is_object())
        arguments = nlohmann::json::object();
    std::string error;
    const std::optional<AgentToolOutcome> outcome = tool->invoke(client, arguments, error);
    if (!outcome)
        return {{"content", {{{"type", "text"}, {"text", error}}}}, {"isError", true}};
    nlohmann::json content = nlohmann::json::array();
    content.push_back(
        {{"type", "text"}, {"text", outcome->result.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)}});
    for (const std::filesystem::path& image : outcome->images) {
        const std::string bytes = ReadImageForModel(
            image, outcome->fullSizeImages ? (std::numeric_limits<uint32_t>::max)() : kModelImageLongSide);
        if (!bytes.empty() && bytes.size() <= kMaximumInlineImageBytes)
            content.push_back({{"type", "image"}, {"data", TEXT::EncodeBase64(bytes)}, {"mimeType", "image/png"}});
    }
    return {{"content", std::move(content)}, {"isError", !outcome->result.value("ok", false)}};
}

} // namespace

int RunMcpServer(AgentClient& client) {
    // 標準入出力は改行区切りの JSON-RPC。CRLF 変換で壊さないようにバイナリにする。stdout には応答だけを書く。
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        const nlohmann::json message = nlohmann::json::parse(line, nullptr, false);
        if (message.is_discarded() || !message.is_object()) {
            ReplyError(nullptr, -32700, "Parse error");
            continue;
        }
        // id の無いものは通知。応答しない。
        if (!message.contains("id"))
            continue;
        const nlohmann::json& id = message["id"];
        const std::string method = message.value("method", std::string{});
        const nlohmann::json params = message.value("params", nlohmann::json::object());
        if (method == "initialize") {
            const std::string requested = params.is_object() ? params.value("protocolVersion", std::string{}) : "";
            std::string_view version = kSupportedProtocols.front();
            for (std::string_view supported : kSupportedProtocols)
                if (supported == requested)
                    version = supported;
            Reply(id, {{"protocolVersion", std::string(version)},
                       {"capabilities", {{"tools", {{"listChanged", false}}}}},
                       {"serverInfo", {{"name", "hikari"}, {"version", "1"}}},
                       {"instructions", AgentUsageInstructions()}});
        } else if (method == "ping") {
            Reply(id, nlohmann::json::object());
        } else if (method == "tools/list") {
            Reply(id, ToolList());
        } else if (method == "tools/call") {
            Reply(id, CallTool(client, params));
        } else {
            ReplyError(id, -32601, "Method not found: " + method);
        }
    }
    return 0;
}

} // namespace HIKARI::AGENT_CLI
