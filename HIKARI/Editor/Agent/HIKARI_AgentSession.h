#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI::EDITOR {
class EditorChildProcess;
}

// 编辑器の中から本机の智能体 CLI（Claude Code、Codex）と一つの会話を続ける。推論は CLI が行い、编辑器の操作は
// CLI が hikari.exe（MCP）経由で本地桥を呼ぶ。利用者の订阅でログインした CLI を使う。
// CLI 自身の道具（命令、文件编辑、浏览器など）の権限確認は编辑器に返ってきて、人が答える。
namespace HIKARI::EDITOR::AGENT {

enum class AgentKind : uint8_t {
    ClaudeCode,
    Codex,
};

inline constexpr TEXT::EnumName<AgentKind> kAgentKindNames[] = {
    {AgentKind::ClaudeCode, "Claude Code"},
    {AgentKind::Codex, "Codex"},
};

// PATH、各 CLI の既定の導入先（Claude 桌面版に同梱の claude.exe、原生安装、npm、Codex 应用に同梱の codex.exe）の順に探す。
// 見つからなければ空。
std::filesystem::path FindAgentExecutable(AgentKind kind);

// CLI に登录しているか（Claude Code は auth status、Codex は login status）。模型は呼ばない。
// 後ろで動かし、Poll で結果を拾う。桌面版に同梱の CLI を単独で起動すると未登录のことがある。
class AgentLoginCheck {
public:
    enum class State : uint8_t {
        Unknown,
        Checking,
        LoggedIn,
        LoggedOut,
        // 調べられなかった（時間切れ、設定の読み込み失敗など）。Detail に CLI の出力の最初の行。
        Failed,
    };
    static constexpr std::chrono::seconds kTimeout{15};

    AgentLoginCheck();
    ~AgentLoginCheck();
    AgentLoginCheck(const AgentLoginCheck&) = delete;
    AgentLoginCheck& operator=(const AgentLoginCheck&) = delete;

    // output は CLI の出力を一時的に置くファイル。
    void Start(AgentKind kind, const std::filesystem::path& executable, const std::filesystem::path& output);
    void Poll();
    State GetState() const noexcept { return state_; }
    const std::string& Detail() const noexcept { return detail_; }
    // 利用者が终端（PowerShell）で打つ登录命令。
    std::string LoginCommand() const;

private:
    std::unique_ptr<EditorChildProcess> process_;
    std::filesystem::path executable_{};
    std::filesystem::path output_{};
    AgentKind kind_ = AgentKind::ClaudeCode;
    State state_ = State::Unknown;
    std::chrono::steady_clock::time_point started_{};
    std::string detail_{};
};

struct AgentEvent {
    enum class Kind : uint8_t {
        User,
        Text,
        ToolCall,
        ToolResult,
        Result,
        Error,
        // 编辑器が聞かずに断った道具（title は理由：engine / content / auto）。
        Blocked,
        // 自動（無需批准）で聞かずに通した道具。
        AutoApproved,
        Raw,
    };
    Kind kind = Kind::Raw;
    // 道具名など短い見出し。
    std::string title{};
    std::string text{};
    bool failed = false;
};

// CLI が実際に報告した用量だけを持つ。報告の無い値は既定のまま（推測しない）。
struct AgentUsage {
    struct Window {
        // five_hour / seven_day（Claude）、primary / secondary（Codex）。
        std::string name{};
        // 0〜100。報告が無ければ負。
        int usedPercent = -1;
        // UNIX 秒。0 は不明。
        int64_t resetsAt = 0;
        int64_t windowMinutes = 0;
    };
    std::vector<Window> windows{};
    // allowed / allowed_warning / rejected（Claude）。
    std::string limitStatus{};
    // pro / plus など（Codex）。
    std::string plan{};
    uint64_t contextTokens = 0;
    uint64_t contextWindow = 0;
    uint64_t turnInputTokens = 0;
    uint64_t turnOutputTokens = 0;
    // Claude の result にある total_cost_usd。API 価格での参考値で、订阅登録では実際に課金されない。
    double referenceCostUsd = -1.0;
};

// 選べる模型。Codex は model/list の報告、Claude Code は CLI の別名。efforts は選べる推理强度。
struct AgentModelOption {
    std::string id{};
    std::string label{};
    std::vector<std::string> efforts{};
    std::string defaultEffort{};
    bool isDefault = false;
    bool operator==(const AgentModelOption&) const = default;
};

// CLI が報告する前から出せる候補（Claude Code の別名と推理强度）。Codex は会話を始めると model/list で埋まる。
std::vector<AgentModelOption> DefaultAgentModels(AgentKind kind);

// CLI の道具の権限確認。
struct AgentToolApproval {
    uint64_t id = 0;
    std::string tool{};
    std::string detail{};
    bool browser = false;
    // 本轮允许で同じものを聞かずに通すための鍵。空なら本轮允许を出さず、毎回聞く。
    std::string grantKey{};
    // 人に知らせる注意（agent.toolApproval.warning.<warning>）。
    std::string warning{};
    // 返答に要る、協議ごとの値。
    nlohmann::json token{};
};

enum class AgentToolDecision : uint8_t {
    AllowOnce,
    // この一轮（智能体が答え終わるまで）は同じ道具を聞かずに通す。
    AllowTurn,
    Deny,
};

struct AgentSessionRequest {
    AgentKind kind = AgentKind::ClaudeCode;
    std::filesystem::path executable{};
    std::filesystem::path projectRoot{};
    std::filesystem::path hikariExecutable{};
    // MCP 設定・出力を置く場所（Library/EditorAI/AgentRuns/<id>）。
    std::filesystem::path runFolder{};
    // 前の会話を続ける（Claude の会話 ID、Codex の thread ID）。
    std::string resumeSession{};
    // 项目访问模式が「开发项目」の時だけ、源码の書き換えを CLI に許す。命令の実行は常に確認する。
    bool allowProjectEdits = false;
    // どの模式でも書き込みを拒む目录（FindEngineRoots）。読むのは構わない。项目がこの中にあれば起動しない。
    std::vector<std::filesystem::path> protectedRoots{};
    std::string model{};
    std::string effort{};
    // Claude in Chrome（Claude Code のみ）。
    bool chrome = false;
    // 利用者が CLI に設定した他の MCP 服务器・插件も読み込む。既定は hikari だけ（道具の定義は毎轮の上下文を使う）。
    bool userTools = false;
    // 自動（無需批准）：CLI の道具の確認を人に聞かずに決める。命令と项目内の编辑は通し、项目の外への書き込みと行き先の
    // 分からない変更は断る（引擎目录と内容模式の禁止は先に効く）。Codex も確認は求めさせ、その場で答える。
    bool autoApprove = false;
};

struct AgentMessage {
    std::string text{};
    // 添付する PNG（視口の截图など）。
    std::vector<std::filesystem::path> images{};
};

class AgentProtocol;

// 一つの会話。Poll を毎フレーム呼ぶ。
class AgentSession {
public:
    AgentSession();
    ~AgentSession();
    AgentSession(const AgentSession&) = delete;
    AgentSession& operator=(const AgentSession&) = delete;

    bool Start(const AgentSessionRequest& request, std::string& outError);
    bool Send(const AgentMessage& message, std::string& outError);
    // 今の一轮を止める。会話は続けられる。
    void Interrupt();
    // 上下文を詰める（Claude Code は /compact、Codex は thread/compact/start）。
    bool Compact(std::string& outError);
    void Poll();
    void Stop() noexcept;

    bool Running() const noexcept;
    // 一轮の途中（返答を待っている）。
    bool Busy() const noexcept { return busy_; }
    // 今の一轮が始まってからの秒数（一轮の外では 0）。
    double BusySeconds() const noexcept;
    // 考えている途中の見積もり token（Claude の thinking_tokens）。文字や道具の記録が届けば 0 に戻る。
    uint64_t ThinkingTokens() const noexcept { return thinkingTokens_; }
    AgentKind Kind() const noexcept { return request_.kind; }
    const std::vector<AgentEvent>& Events() const noexcept { return events_; }
    const std::vector<AgentToolApproval>& PendingApprovals() const noexcept { return approvals_; }
    void ResolveApproval(uint64_t id, AgentToolDecision decision);
    const AgentUsage& Usage() const noexcept { return usage_; }
    const std::string& SessionId() const noexcept { return sessionId_; }
    // CLI が報告した模型（Codex の model/list）。報告が無ければ空。
    const std::vector<AgentModelOption>& Models() const noexcept { return models_; }

    // 以下は協議（AgentProtocol）から使う。
    bool WriteLine(const nlohmann::json& message);
    void AddEvent(AgentEvent event);
    // 本轮允许で既に許されていれば true を返し、承認待ちには入れない（呼び出し側がすぐ許可を返す）。
    bool AddApproval(AgentToolApproval approval);
    // token[key] が value の承認待ちを消す（CLI が確認を取り下げた時）。
    void RemoveApproval(std::string_view key, const nlohmann::json& value);
    void SetSessionId(std::string id) { sessionId_ = std::move(id); }
    void SetModels(std::vector<AgentModelOption> models) { models_ = std::move(models); }
    void SetBusy(bool busy);
    void SetThinkingTokens(uint64_t tokens) noexcept { thinkingTokens_ = tokens; }
    AgentUsage& MutableUsage() noexcept { return usage_; }
    const AgentSessionRequest& Request() const noexcept { return request_; }

private:
    void ReadNewOutput();

    std::unique_ptr<AgentProtocol> protocol_;
    std::unique_ptr<EditorChildProcess> process_;
    AgentSessionRequest request_{};
    std::filesystem::path output_{};
    uint64_t readOffset_ = 0;
    std::string partialLine_{};
    std::vector<AgentEvent> events_{};
    std::vector<AgentToolApproval> approvals_{};
    std::set<std::string, std::less<>> turnGrants_{};
    uint64_t nextApprovalId_ = 1;
    AgentUsage usage_{};
    std::vector<AgentModelOption> models_{};
    std::string sessionId_{};
    bool busy_ = false;
    std::chrono::steady_clock::time_point busySince_{};
    uint64_t thinkingTokens_ = 0;
    bool exited_ = false;
};

} // namespace HIKARI::EDITOR::AGENT
