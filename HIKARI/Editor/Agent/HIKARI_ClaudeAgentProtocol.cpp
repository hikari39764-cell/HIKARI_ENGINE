// Claude Code：-p --input-format stream-json --output-format stream-json の常駐会話。
// 利用者の発言は標準入力の {"type":"user"} 行、道具の権限確認は control_request（can_use_tool）で编辑器に来る
// （--permission-prompts host）。用量は rate_limit_event・result・get_context_usage から読む。
#include "Editor/Agent/HIKARI_AgentProtocol.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#include "Core/Text/HIKARI_Base64.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI::EDITOR::AGENT {
namespace {

// 道具名から MCP の接頭辞（mcp__hikari__）を外す。
std::string ToolTitle(std::string name) {
    constexpr std::string_view kPrefix = "mcp__hikari__";
    return name.starts_with(kPrefix) ? name.substr(kPrefix.size()) : name;
}

nlohmann::json ControlRequest(std::string id, nlohmann::json request) {
    return {{"type", "control_request"}, {"request_id", std::move(id)}, {"request", std::move(request)}};
}

class ClaudeProtocol final : public AgentProtocol {
public:
    bool BuildCommand(const AgentSessionRequest& request, AgentCommand& outCommand, std::string& outError) override {
        const std::filesystem::path config = request.runFolder / "mcp.json";
        const nlohmann::json mcp{
            {"mcpServers",
             {{"hikari",
               {{"command", AgentGenericPath(request.hikariExecutable)},
                {"args", nlohmann::json::array({"mcp", "--project", AgentGenericPath(request.projectRoot)})}}}}}};
        std::ofstream output(config, std::ios::binary | std::ios::trunc);
        output << mcp.dump(2);
        if (!output) {
            outError = "Cannot write the MCP configuration for this conversation.";
            return false;
        }
        std::vector<std::wstring>& arguments = outCommand.arguments;
        const auto add = [&arguments](const std::string& text) {
            arguments.push_back(TEXT::PathFromUtf8(text).wstring());
        };
        for (const char* argument : {"-p", "--input-format", "stream-json", "--output-format", "stream-json",
                                     "--verbose", "--permission-prompts", "host"})
            add(argument);
        add("--mcp-config");
        add(AgentGenericPath(config));
        // hikari の道具と読むだけの道具は聞かずに通す。编辑器の操作には编辑器自身の承認がある。
        add("--allowedTools");
        add("mcp__hikari Read Glob Grep");
        if (request.allowProjectEdits) {
            add("--permission-mode");
            add("acceptEdits");
        } else {
            add("--disallowedTools");
            add("Edit Write MultiEdit NotebookEdit");
        }
        const auto option = [&add](const char* name, const std::string& value) {
            if (!value.empty()) {
                add(name);
                add(value);
            }
        };
        option("--resume", request.resumeSession);
        option("--model", request.model);
        option("--effort", request.effort);
        if (request.chrome)
            add("--chrome");
        return ResolveAgentLauncher(request.executable, outCommand, outError);
    }

    void OnStarted(AgentSession& session) override {
        (void)session.WriteLine(ControlRequest("hikari-init", {{"subtype", "initialize"}}));
    }

    bool Send(AgentSession& session, const AgentMessage& message, std::string& outError) override {
        nlohmann::json content = nlohmann::json::array({{{"type", "text"}, {"text", message.text}}});
        for (const std::filesystem::path& image : message.images) {
            std::ifstream input(image, std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            if (bytes.empty())
                continue;
            content.push_back(
                {{"type", "image"},
                 {"source", {{"type", "base64"}, {"media_type", "image/png"}, {"data", TEXT::EncodeBase64(bytes)}}}});
        }
        const nlohmann::json line{{"type", "user"},
                                  {"message", {{"role", "user"}, {"content", std::move(content)}}},
                                  {"parent_tool_use_id", nullptr}};
        if (session.WriteLine(line))
            return true;
        outError = "Cannot write to Claude Code.";
        return false;
    }

    void Interrupt(AgentSession& session) override {
        (void)session.WriteLine(
            ControlRequest("hikari-interrupt-" + std::to_string(++requestSerial_), {{"subtype", "interrupt"}}));
    }

    void HandleLine(AgentSession& session, const nlohmann::json& line) override {
        if (line.contains("session_id") && line["session_id"].is_string())
            session.SetSessionId(line["session_id"].get<std::string>());
        const std::string type = line.value("type", std::string{});
        const nlohmann::json content = line.contains("message") && line["message"].is_object()
                                           ? line["message"].value("content", nlohmann::json::array())
                                           : nlohmann::json::array();
        if (type == "assistant") {
            for (const nlohmann::json& item : content) {
                const std::string kind = item.value("type", std::string{});
                if (kind == "text" && !item.value("text", std::string{}).empty())
                    session.AddEvent({AgentEvent::Kind::Text, {}, ClipAgentText(item.value("text", std::string{}))});
                else if (kind == "tool_use")
                    session.AddEvent({AgentEvent::Kind::ToolCall, ToolTitle(item.value("name", std::string{})),
                                      ClipAgentText(AgentJsonText(item.value("input", nlohmann::json::object())))});
            }
        } else if (type == "user") {
            for (const nlohmann::json& item : content) {
                if (item.value("type", std::string{}) == "tool_result")
                    session.AddEvent({AgentEvent::Kind::ToolResult,
                                      {},
                                      ClipAgentText(AgentJsonText(item.value("content", nlohmann::json{}))),
                                      item.value("is_error", false)});
            }
        } else if (type == "result") {
            HandleResult(session, line);
        } else if (type == "rate_limit_event") {
            HandleRateLimit(session, line.value("rate_limit_info", nlohmann::json::object()));
        } else if (type == "control_request") {
            HandleControlRequest(session, line);
        } else if (type == "control_response") {
            const nlohmann::json response = line.value("response", nlohmann::json::object());
            if (response.value("request_id", std::string{}).starts_with("hikari-context") &&
                response.value("subtype", std::string{}) == "success") {
                const nlohmann::json usage = response.value("response", nlohmann::json::object());
                session.MutableUsage().contextTokens = usage.value("totalTokens", uint64_t{0});
                session.MutableUsage().contextWindow = usage.value("maxTokens", uint64_t{0});
            }
        } else if (type == "control_cancel_request") {
            session.RemoveApproval("request_id", line.value("request_id", std::string{}));
        } else if (type == "system") {
            for (const nlohmann::json& server : line.value("mcp_servers", nlohmann::json::array())) {
                if (server.value("name", std::string{}) == "hikari" &&
                    server.value("status", std::string{}) != "connected")
                    session.AddEvent({AgentEvent::Kind::Error, "MCP",
                                      "hikari MCP server: " + server.value("status", std::string("unknown")), true});
            }
        }
    }

    void Respond(AgentSession& session, const AgentToolApproval& approval, bool allow) override {
        const std::string id = approval.token.value("request_id", std::string{});
        nlohmann::json decision =
            allow ? nlohmann::json{{"behavior", "allow"},
                                   {"updatedInput", approval.token.value("input", nlohmann::json::object())}}
                  : nlohmann::json{{"behavior", "deny"}, {"message", "The user denied this in the HIKARI editor."}};
        (void)session.WriteLine(
            {{"type", "control_response"},
             {"response", {{"subtype", "success"}, {"request_id", id}, {"response", std::move(decision)}}}});
    }

private:
    void HandleResult(AgentSession& session, const nlohmann::json& line) {
        AgentUsage& usage = session.MutableUsage();
        const nlohmann::json tokens = line.value("usage", nlohmann::json::object());
        usage.turnInputTokens = tokens.value("input_tokens", uint64_t{0}) +
                                tokens.value("cache_read_input_tokens", uint64_t{0}) +
                                tokens.value("cache_creation_input_tokens", uint64_t{0});
        usage.turnOutputTokens = tokens.value("output_tokens", uint64_t{0});
        if (line.contains("total_cost_usd") && line["total_cost_usd"].is_number())
            usage.referenceCostUsd = line["total_cost_usd"].get<double>();
        const bool failed = line.value("is_error", false) || line.value("subtype", std::string{}) != "success";
        session.AddEvent({AgentEvent::Kind::Result, std::to_string(line.value("num_turns", 0)) + " turns",
                          ClipAgentText(AgentJsonText(line.value("result", nlohmann::json{}))), failed});
        session.SetBusy(false);
        (void)session.WriteLine(
            ControlRequest("hikari-context-" + std::to_string(++requestSerial_), {{"subtype", "get_context_usage"}}));
    }

    // 窗口ごとに最新の値で置き換える。utilization は 0〜1。
    static void HandleRateLimit(AgentSession& session, const nlohmann::json& info) {
        AgentUsage& usage = session.MutableUsage();
        usage.limitStatus = info.value("status", usage.limitStatus);
        const std::string name = info.value("rateLimitType", std::string{});
        if (name.empty())
            return;
        auto window = std::find_if(usage.windows.begin(), usage.windows.end(),
                                   [&name](const AgentUsage::Window& entry) { return entry.name == name; });
        if (window == usage.windows.end())
            window = usage.windows.insert(usage.windows.end(), AgentUsage::Window{name});
        if (info.contains("utilization") && info["utilization"].is_number())
            window->usedPercent = static_cast<int>(std::floor(info["utilization"].get<double>() * 100.0));
        if (info.contains("resetsAt") && info["resetsAt"].is_number())
            window->resetsAt = info["resetsAt"].get<int64_t>();
    }

    void HandleControlRequest(AgentSession& session, const nlohmann::json& line) {
        const std::string id = line.value("request_id", std::string{});
        const nlohmann::json request = line.value("request", nlohmann::json::object());
        if (request.value("subtype", std::string{}) != "can_use_tool") {
            (void)session.WriteLine(
                {{"type", "control_response"},
                 {"response",
                  {{"subtype", "error"}, {"request_id", id}, {"error", "Not supported by the HIKARI editor."}}}});
            return;
        }
        const std::string tool = request.value("tool_name", std::string{});
        AgentToolApproval approval;
        approval.tool = request.value("display_name", tool);
        approval.detail = ClipAgentText(
            request.value("description", AgentJsonText(request.value("input", nlohmann::json::object()))));
        approval.browser = request.value("tool_kind", std::string{}) == "browser";
        approval.grantKey = tool;
        approval.token = {{"request_id", id}, {"input", request.value("input", nlohmann::json::object())}};
        if (session.AddApproval(approval))
            Respond(session, approval, true);
    }

    uint64_t requestSerial_ = 0;
};

} // namespace

std::unique_ptr<AgentProtocol> CreateClaudeProtocol() { return std::make_unique<ClaudeProtocol>(); }

} // namespace HIKARI::EDITOR::AGENT
