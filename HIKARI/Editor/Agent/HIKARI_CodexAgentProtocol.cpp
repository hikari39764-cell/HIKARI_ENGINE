// Codex：codex app-server（標準入出力の JSON-RPC）。initialize → thread/start（または thread/resume）→ turn/start。
// 命令の実行・文件の変更・権限の追加は服务端からの要求（item/*/requestApproval）で编辑器に来る。
// 用量は account/rateLimits/read と account/rateLimits/updated、上下文は thread/tokenUsage/updated から読む。
#include "Editor/Agent/HIKARI_AgentProtocol.h"

#include <map>
#include <optional>
#include <utility>

#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI::EDITOR::AGENT {
namespace {

class CodexProtocol final : public AgentProtocol {
public:
    bool BuildCommand(const AgentSessionRequest& request, AgentCommand& outCommand, std::string& outError) override {
        const std::string hikari = AgentGenericPath(request.hikariExecutable);
        const std::string project = AgentGenericPath(request.projectRoot);
        // TOML の文字列。パスは / 区切りなのでエスケープが要らない。
        for (const std::string& argument :
             {std::string("app-server"), std::string("-c"), "mcp_servers.hikari.command=\"" + hikari + "\"",
              std::string("-c"), "mcp_servers.hikari.args=[\"mcp\", \"--project\", \"" + project + "\"]"})
            outCommand.arguments.push_back(TEXT::PathFromUtf8(argument).wstring());
        return ResolveAgentLauncher(request.executable, outCommand, outError);
    }

    void OnStarted(AgentSession& session) override {
        const AgentSessionRequest& request = session.Request();
        Call(session, "initialize",
             {{"clientInfo", {{"name", "hikari"}, {"title", "HIKARI Editor"}, {"version", "1"}}}}, Purpose::Initialize);
        (void)session.WriteLine({{"method", "initialized"}});
        Call(session, "account/rateLimits/read", nullptr, Purpose::RateLimits);
        nlohmann::json thread{{"cwd", AgentGenericPath(request.projectRoot)},
                              {"sandbox", request.allowProjectEdits ? "workspace-write" : "read-only"},
                              {"approvalPolicy", "on-request"}};
        if (!request.model.empty())
            thread["model"] = request.model;
        if (request.resumeSession.empty()) {
            Call(session, "thread/start", std::move(thread), Purpose::Thread);
        } else {
            thread["threadId"] = request.resumeSession;
            Call(session, "thread/resume", std::move(thread), Purpose::Thread);
        }
    }

    bool Send(AgentSession& session, const AgentMessage& message, std::string& outError) override {
        nlohmann::json input = nlohmann::json::array({{{"type", "text"}, {"text", message.text}}});
        for (const std::filesystem::path& image : message.images)
            input.push_back({{"type", "localImage"}, {"path", AgentGenericPath(image)}});
        // thread がまだ無ければ、thread/start の返事を待ってから送る。
        pendingInput_ = std::move(input);
        if (!threadId_.empty())
            StartTurn(session);
        (void)outError;
        return true;
    }

    void Interrupt(AgentSession& session) override {
        if (!threadId_.empty() && !turnId_.empty())
            Call(session, "turn/interrupt", {{"threadId", threadId_}, {"turnId", turnId_}}, Purpose::Other);
    }

    void HandleLine(AgentSession& session, const nlohmann::json& line) override {
        const bool hasId = line.contains("id") && !line["id"].is_null();
        if (hasId && line.contains("method"))
            return HandleServerRequest(session, line);
        if (hasId)
            return HandleResponse(session, line);
        const std::string method = line.value("method", std::string{});
        const nlohmann::json params = line.value("params", nlohmann::json::object());
        if (method == "item/completed") {
            HandleItem(session, params.value("item", nlohmann::json::object()));
        } else if (method == "turn/started") {
            turnId_ = params.value("turn", nlohmann::json::object()).value("id", turnId_);
        } else if (method == "turn/completed") {
            const nlohmann::json turn = params.value("turn", nlohmann::json::object());
            const std::string status = turn.value("status", std::string{});
            if (status == "failed" && turn.contains("error") && turn["error"].is_object())
                session.AddEvent(
                    {AgentEvent::Kind::Error, {}, ClipAgentText(turn["error"].value("message", std::string{})), true});
            session.AddEvent({AgentEvent::Kind::Result, status, {}, status == "failed"});
            turnId_.clear();
            session.SetBusy(false);
        } else if (method == "thread/tokenUsage/updated") {
            const nlohmann::json usage = params.value("tokenUsage", nlohmann::json::object());
            const nlohmann::json last = usage.value("last", nlohmann::json::object());
            AgentUsage& target = session.MutableUsage();
            target.contextTokens = last.value("totalTokens", uint64_t{0});
            if (usage.contains("modelContextWindow") && usage["modelContextWindow"].is_number())
                target.contextWindow = usage["modelContextWindow"].get<uint64_t>();
            target.turnInputTokens = last.value("inputTokens", uint64_t{0});
            target.turnOutputTokens = last.value("outputTokens", uint64_t{0});
        } else if (method == "account/rateLimits/updated") {
            ReadRateLimits(session, params.value("rateLimits", nlohmann::json::object()));
        } else if (method == "error" && !params.value("willRetry", false)) {
            session.AddEvent(
                {AgentEvent::Kind::Error,
                 {},
                 ClipAgentText(params.value("error", nlohmann::json::object()).value("message", std::string{})),
                 true});
        }
    }

    void Respond(AgentSession& session, const AgentToolApproval& approval, bool allow) override {
        const std::string method = approval.token.value("method", std::string{});
        nlohmann::json result;
        if (method == "item/permissions/requestApproval")
            result =
                allow ? nlohmann::json{{"permissions", approval.token.value("permissions", nlohmann::json::object())},
                                       {"scope", "turn"}}
                      : nlohmann::json{{"permissions", nlohmann::json::object()}};
        else
            result = {{"decision", allow ? "accept" : "decline"}};
        (void)session.WriteLine({{"id", approval.token.at("id")}, {"result", std::move(result)}});
    }

private:
    enum class Purpose : uint8_t { Initialize, RateLimits, Thread, Turn, Other };

    void Call(AgentSession& session, const char* method, nlohmann::json params, Purpose purpose) {
        const int64_t id = nextId_++;
        purposes_[id] = purpose;
        (void)session.WriteLine({{"id", id}, {"method", method}, {"params", std::move(params)}});
    }

    void StartTurn(AgentSession& session) {
        if (!pendingInput_)
            return;
        nlohmann::json turn{{"threadId", threadId_}, {"input", std::move(*pendingInput_)}};
        pendingInput_.reset();
        const AgentSessionRequest& request = session.Request();
        if (!request.model.empty())
            turn["model"] = request.model;
        if (!request.effort.empty())
            turn["effort"] = request.effort;
        Call(session, "turn/start", std::move(turn), Purpose::Turn);
    }

    void HandleResponse(AgentSession& session, const nlohmann::json& line) {
        const int64_t id = line["id"].is_number_integer() ? line["id"].get<int64_t>() : -1;
        const auto found = purposes_.find(id);
        const Purpose purpose = found != purposes_.end() ? found->second : Purpose::Other;
        if (found != purposes_.end())
            purposes_.erase(found);
        if (line.contains("error")) {
            const nlohmann::json error = line["error"];
            session.AddEvent({AgentEvent::Kind::Error,
                              {},
                              ClipAgentText(error.is_object() ? error.value("message", error.dump()) : error.dump()),
                              true});
            if (purpose == Purpose::Turn || purpose == Purpose::Thread)
                session.SetBusy(false);
            return;
        }
        const nlohmann::json result = line.value("result", nlohmann::json::object());
        if (purpose == Purpose::Thread) {
            threadId_ = result.value("thread", nlohmann::json::object()).value("id", std::string{});
            session.SetSessionId(threadId_);
            StartTurn(session);
        } else if (purpose == Purpose::Turn) {
            turnId_ = result.value("turn", nlohmann::json::object()).value("id", turnId_);
        } else if (purpose == Purpose::RateLimits) {
            ReadRateLimits(session, result.value("rateLimits", nlohmann::json::object()));
        }
    }

    static void ReadRateLimits(AgentSession& session, const nlohmann::json& snapshot) {
        AgentUsage& usage = session.MutableUsage();
        usage.windows.clear();
        for (const char* name : {"primary", "secondary"}) {
            if (!snapshot.contains(name) || !snapshot[name].is_object())
                continue;
            const nlohmann::json& window = snapshot[name];
            AgentUsage::Window entry{name};
            entry.usedPercent = window.value("usedPercent", -1);
            if (window.contains("resetsAt") && window["resetsAt"].is_number())
                entry.resetsAt = window["resetsAt"].get<int64_t>();
            if (window.contains("windowDurationMins") && window["windowDurationMins"].is_number())
                entry.windowMinutes = window["windowDurationMins"].get<int64_t>();
            usage.windows.push_back(std::move(entry));
        }
        if (snapshot.contains("planType") && snapshot["planType"].is_string())
            usage.plan = snapshot["planType"].get<std::string>();
    }

    void HandleItem(AgentSession& session, const nlohmann::json& item) {
        const std::string kind = item.value("type", std::string{});
        if (kind == "agentMessage") {
            session.AddEvent({AgentEvent::Kind::Text, {}, ClipAgentText(item.value("text", std::string{}))});
        } else if (kind == "commandExecution") {
            session.AddEvent(
                {AgentEvent::Kind::ToolCall, "command", ClipAgentText(item.value("command", std::string{}))});
            const nlohmann::json exitCode = item.value("exitCode", nlohmann::json{});
            session.AddEvent({AgentEvent::Kind::ToolResult,
                              {},
                              ClipAgentText(item.value("aggregatedOutput", std::string{})),
                              exitCode.is_number_integer() && exitCode.get<int64_t>() != 0});
        } else if (kind == "mcpToolCall") {
            const nlohmann::json error = item.value("error", nlohmann::json{});
            session.AddEvent({AgentEvent::Kind::ToolCall, item.value("tool", std::string{}),
                              ClipAgentText(AgentJsonText(item.value("arguments", nlohmann::json{}))),
                              !error.is_null() || item.value("status", std::string{}) == "failed"});
        } else if (kind == "fileChange") {
            session.AddEvent({AgentEvent::Kind::ToolCall, "file_change",
                              ClipAgentText(AgentJsonText(item.value("changes", nlohmann::json{})))});
        } else if (kind == "webSearch") {
            session.AddEvent(
                {AgentEvent::Kind::ToolCall, "web_search", ClipAgentText(item.value("query", std::string{}))});
        }
    }

    void HandleServerRequest(AgentSession& session, const nlohmann::json& line) {
        const std::string method = line.value("method", std::string{});
        const nlohmann::json params = line.value("params", nlohmann::json::object());
        AgentToolApproval approval;
        approval.token = {{"id", line["id"]}, {"method", method}};
        if (method == "item/commandExecution/requestApproval") {
            approval.tool = "command";
            approval.detail = params.value("command", std::string{});
            if (params.contains("reason") && params["reason"].is_string())
                approval.detail += "\n" + params["reason"].get<std::string>();
            approval.grantKey = "command";
        } else if (method == "item/fileChange/requestApproval") {
            approval.tool = "file_change";
            approval.detail = params.contains("reason") && params["reason"].is_string()
                                  ? params["reason"].get<std::string>()
                                  : std::string{};
            approval.grantKey = "file_change";
        } else if (method == "item/permissions/requestApproval") {
            approval.tool = "permissions";
            approval.detail = AgentJsonText(params.value("permissions", nlohmann::json::object()));
            approval.token["permissions"] = params.value("permissions", nlohmann::json::object());
            approval.grantKey = "permissions";
        } else {
            // 编辑器が答えられない要求（利用者入力、MCP の確認、旧形式の承認など）は断り、turn を止めない。
            (void)session.WriteLine(
                {{"id", line["id"]},
                 {"error", {{"code", -32601}, {"message", "Not supported by the HIKARI editor."}}}});
            return;
        }
        approval.detail = ClipAgentText(approval.detail);
        if (session.AddApproval(approval))
            Respond(session, approval, true);
    }

    int64_t nextId_ = 1;
    std::map<int64_t, Purpose> purposes_{};
    std::string threadId_{};
    std::string turnId_{};
    std::optional<nlohmann::json> pendingInput_{};
};

} // namespace

std::unique_ptr<AgentProtocol> CreateCodexProtocol() { return std::make_unique<CodexProtocol>(); }

} // namespace HIKARI::EDITOR::AGENT
