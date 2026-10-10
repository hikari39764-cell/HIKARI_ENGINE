// Codex：codex app-server（標準入出力の JSON-RPC）。initialize → thread/start（または thread/resume）→ turn/start。
// 命令の実行・文件の変更・権限の追加は服务端からの要求（item/*/requestApproval）で编辑器に来る。
// 沙箱は毎轮明示する（内容は只读、开发项目は项目と临时目录だけ書ける）。沙箱の外に出る要求は、引擎目录に触れるものと
// 内容模式の文件变更を聞かずに断る。用量は account/rateLimits/read と account/rateLimits/updated、上下文は
// thread/tokenUsage/updated から読む。
#include "Editor/Agent/HIKARI_AgentProtocol.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <optional>
#include <string_view>
#include <utility>

#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI::EDITOR::AGENT {
namespace {

std::string StringField(const nlohmann::json& object, const char* key) {
    if (!object.is_object())
        return {};
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

// 追加の書き込み許可（item/permissions/requestApproval の permissions.fileSystem）が引擎目录に届くか。
// glob はワイルドカードの前の目录で見て、分からない形は届くものとして扱う。
bool GrantsEngineWrite(const AgentSessionRequest& request, const nlohmann::json& permissions,
                       const std::filesystem::path& cwd) {
    const auto touches = [&request, &cwd](const std::string& path) {
        return TouchesEngineRoots(request.protectedRoots, TEXT::PathFromUtf8(path), cwd);
    };
    const nlohmann::json fileSystem =
        permissions.is_object() ? permissions.value("fileSystem", nlohmann::json{}) : nlohmann::json{};
    if (!fileSystem.is_object())
        return false;
    for (const nlohmann::json& path : fileSystem.value("write", nlohmann::json::array()))
        if (!path.is_string() || touches(path.get<std::string>()))
            return true;
    for (const nlohmann::json& entry : fileSystem.value("entries", nlohmann::json::array())) {
        if (StringField(entry, "access") != "write")
            continue;
        const nlohmann::json target = entry.value("path", nlohmann::json::object());
        const std::string type = StringField(target, "type");
        if (type == "path") {
            if (touches(StringField(target, "path")))
                return true;
        } else if (type == "glob_pattern") {
            const std::string pattern = StringField(target, "pattern");
            const size_t folder = pattern.find_last_of("/\\", pattern.find_first_of("*?[{"));
            if (folder == std::string::npos || touches(pattern.substr(0u, folder + 1u)))
                return true;
        } else if (type == "special") {
            const nlohmann::json value = target.value("value", nlohmann::json::object());
            const std::string kind = StringField(value, "kind");
            if (kind == "project_roots") {
                if (touches(AgentGenericPath(request.projectRoot / TEXT::PathFromUtf8(StringField(value, "subpath")))))
                    return true;
            } else if (kind != "tmpdir" && kind != "slash_tmp" && kind != "minimal") {
                return true;
            }
        } else {
            return true;
        }
    }
    return false;
}

// 利用者の config.toml にある MCP 服务器と插件を一つずつ切る -c（Codex には全部を切る旗が無い）。
// 名前に . を含むものは -c の键で書けないので残る。
std::vector<std::string> UserToolOverrides() {
    std::filesystem::path root = AgentEnvironmentPath(L"CODEX_HOME");
    if (root.empty())
        root = AgentEnvironmentPath(L"USERPROFILE") / ".codex";
    std::vector<std::string> overrides;
    std::ifstream input(root / "config.toml");
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        for (const std::string_view table : {std::string_view("mcp_servers"), std::string_view("plugins")}) {
            const std::string prefix = "[" + std::string(table) + ".";
            if (!line.starts_with(prefix) || !line.ends_with("]"))
                continue;
            std::string name = line.substr(prefix.size(), line.size() - prefix.size() - 1u);
            if (name.size() >= 2u && name.front() == '"' && name.back() == '"')
                name = name.substr(1u, name.size() - 2u);
            if (!name.empty() && name.find_first_of(".\"[] ") == std::string::npos && name != "hikari")
                overrides.push_back(std::string(table) + "." + name + ".enabled=false");
        }
    }
    return overrides;
}

class CodexProtocol final : public AgentProtocol {
public:
    bool BuildCommand(const AgentSessionRequest& request, AgentCommand& outCommand, std::string& outError) override {
        const std::string hikari = AgentGenericPath(request.hikariExecutable);
        const std::string project = AgentGenericPath(request.projectRoot);
        // TOML の文字列。パスは / 区切りなのでエスケープが要らない。hikari の道具は聞かずに通す（编辑器の操作には
        // 编辑器自身の承認がある。app-server が MCP の確認を求めると、编辑器は答えられず道具が失敗する）。
        for (const std::string& argument :
             {std::string("app-server"), std::string("-c"), "mcp_servers.hikari.command=\"" + hikari + "\"",
              std::string("-c"), "mcp_servers.hikari.args=[\"mcp\", \"--project\", \"" + project + "\"]",
              std::string("-c"), std::string("mcp_servers.hikari.default_tools_approval_mode=\"approve\"")})
            outCommand.arguments.push_back(TEXT::PathFromUtf8(argument).wstring());
        if (!request.userTools) {
            for (const std::string& disabled : UserToolOverrides()) {
                outCommand.arguments.push_back(L"-c");
                outCommand.arguments.push_back(TEXT::PathFromUtf8(disabled).wstring());
            }
        }
        return ResolveAgentLauncher(request.executable, outCommand, outError);
    }

    void OnStarted(AgentSession& session) override {
        const AgentSessionRequest& request = session.Request();
        Call(session, "initialize",
             {{"clientInfo", {{"name", "hikari"}, {"title", "HIKARI Editor"}, {"version", "1"}}}}, Purpose::Initialize);
        (void)session.WriteLine({{"method", "initialized"}});
        Call(session, "account/rateLimits/read", nullptr, Purpose::RateLimits);
        Call(session, "model/list", {{"limit", 50}}, Purpose::Models);
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

    bool Compact(AgentSession& session, std::string& outError) override {
        if (threadId_.empty()) {
            outError = "The conversation has not started yet.";
            return false;
        }
        Call(session, "thread/compact/start", {{"threadId", threadId_}}, Purpose::Other);
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
        if (method == "item/started") {
            // 文件变更の承認要求は item ID しか持たないので、変更先（移動先も）をここで覚える。
            const nlohmann::json item = params.value("item", nlohmann::json::object());
            if (StringField(item, "type") == "fileChange") {
                std::vector<std::string>& paths = fileChanges_[StringField(item, "id")];
                for (const nlohmann::json& change : item.value("changes", nlohmann::json::array())) {
                    paths.push_back(StringField(change, "path"));
                    const std::string moved =
                        change.is_object() ? StringField(change.value("kind", nlohmann::json{}), "move_path") : "";
                    if (!moved.empty())
                        paths.push_back(moved);
                }
            }
        } else if (method == "item/completed") {
            const nlohmann::json item = params.value("item", nlohmann::json::object());
            HandleItem(session, item);
            fileChanges_.erase(StringField(item, "id"));
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
            fileChanges_.clear();
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
    enum class Purpose : uint8_t { Initialize, RateLimits, Models, Thread, Turn, Other };

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
        // 利用者の config.toml にある writable_roots を引き継がない。
        turn["sandboxPolicy"] = request.allowProjectEdits ? nlohmann::json{{"type", "workspaceWrite"},
                                                                           {"writableRoots", nlohmann::json::array()},
                                                                           {"networkAccess", false}}
                                                          : nlohmann::json{{"type", "readOnly"}};
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
        } else if (purpose == Purpose::Models) {
            std::vector<AgentModelOption> models;
            for (const nlohmann::json& model : result.value("data", nlohmann::json::array())) {
                if (!model.is_object() || model.value("hidden", false))
                    continue;
                AgentModelOption option{StringField(model, "model"),
                                        StringField(model, "displayName"),
                                        {},
                                        StringField(model, "defaultReasoningEffort"),
                                        model.value("isDefault", false)};
                for (const nlohmann::json& effort : model.value("supportedReasoningEfforts", nlohmann::json::array()))
                    if (const std::string value = StringField(effort, "reasoningEffort"); !value.empty())
                        option.efforts.push_back(value);
                if (!option.id.empty())
                    models.push_back(std::move(option));
            }
            session.SetModels(std::move(models));
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

    // 聞かずに断り、記録に残す。reason は agent.event.blocked.<reason>。
    void Block(AgentSession& session, const AgentToolApproval& approval, const char* reason,
               const std::string& target) {
        session.AddEvent({AgentEvent::Kind::Blocked, reason, ClipAgentText(approval.tool + ": " + target), true});
        Respond(session, approval, false);
    }

    void HandleServerRequest(AgentSession& session, const nlohmann::json& line) {
        const std::string method = line.value("method", std::string{});
        const nlohmann::json params = line.value("params", nlohmann::json::object());
        const AgentSessionRequest& request = session.Request();
        const std::string reason = StringField(params, "reason");
        const auto withReason = [&reason](std::string text) { return reason.empty() ? text : text + "\n" + reason; };
        AgentToolApproval approval;
        approval.token = {{"id", line["id"]}, {"method", method}};
        if (method == "item/commandExecution/requestApproval") {
            // 承認すると沙箱の外で動く。命令の文と作業目录で引擎目录を見る。
            const std::string command = StringField(params, "command");
            const std::string cwdText = StringField(params, "cwd");
            const std::filesystem::path cwd = TEXT::PathFromUtf8(cwdText);
            approval.tool = "command";
            // 相対パスや cd で引擎目录に届く命令は文では見分けられないので、人が作業目录も見て決められるようにする。
            approval.detail = withReason(cwdText.empty() ? command : command + "\n(cwd " + cwdText + ")");
            approval.grantKey = "command";
            approval.warning = "command";
            if (MentionsEngineRoots(request.protectedRoots, command) ||
                std::any_of(request.protectedRoots.begin(), request.protectedRoots.end(),
                            [&cwd](const std::filesystem::path& root) { return IsInsideAgentRoot(cwd, root); }))
                return Block(session, approval, "engine", command);
        } else if (method == "item/fileChange/requestApproval") {
            const auto found = fileChanges_.find(StringField(params, "itemId"));
            const std::vector<std::string> paths =
                found != fileChanges_.end() ? found->second : std::vector<std::string>{};
            const std::string grantRoot = StringField(params, "grantRoot");
            std::string targets;
            for (const std::string& path : paths)
                targets += (targets.empty() ? "" : "\n") + path;
            if (!grantRoot.empty())
                targets += (targets.empty() ? "" : "\n") + grantRoot;
            approval.tool = "file_change";
            approval.detail = withReason(targets);
            approval.grantKey = "file_change";
            const auto touchesEngine = [&request](const std::string& path) {
                return TouchesEngineRoots(request.protectedRoots, TEXT::PathFromUtf8(path), request.projectRoot);
            };
            if (std::any_of(paths.begin(), paths.end(), touchesEngine) || touchesEngine(grantRoot))
                return Block(session, approval, "engine", targets);
            if (!request.allowProjectEdits)
                return Block(session, approval, "content", targets);
            const auto insideProject = [&request](const std::string& path) {
                return IsInsideAgentRoot(TEXT::PathFromUtf8(path), request.projectRoot, request.projectRoot);
            };
            if (!paths.empty() && grantRoot.empty() && std::all_of(paths.begin(), paths.end(), insideProject))
                return Respond(session, approval, true);
            approval.warning = "outsideProject";
            // 変更先が分からない時は人に見せる目標が無い。本轮允许では通さず、毎回聞く。
            if (paths.empty() && grantRoot.empty()) {
                approval.warning = "unknownTarget";
                approval.grantKey.clear();
            }
        } else if (method == "item/permissions/requestApproval") {
            const nlohmann::json permissions = params.value("permissions", nlohmann::json::object());
            const std::string cwd = StringField(params, "cwd");
            approval.tool = "permissions";
            approval.detail = withReason(AgentJsonText(permissions));
            approval.token["permissions"] = permissions;
            approval.grantKey = "permissions";
            approval.warning = "permissions";
            if (GrantsEngineWrite(request, permissions, cwd.empty() ? request.projectRoot : TEXT::PathFromUtf8(cwd)))
                return Block(session, approval, "engine", approval.detail);
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
    // 進行中の文件变更の item ID と変更先。
    std::map<std::string, std::vector<std::string>> fileChanges_{};
};

} // namespace

std::unique_ptr<AgentProtocol> CreateCodexProtocol() { return std::make_unique<CodexProtocol>(); }

} // namespace HIKARI::EDITOR::AGENT
