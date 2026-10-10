#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <json.hpp>

#include "Editor/Agent/HIKARI_AgentSession.h"

// AgentSession の内部。AgentSession が子進程と出力の行を持ち、協議は行の意味と送る形を持つ。
namespace HIKARI::EDITOR::AGENT {

struct AgentCommand {
    std::filesystem::path executable{};
    std::vector<std::wstring> arguments{};
};

class AgentProtocol {
public:
    virtual ~AgentProtocol() = default;
    virtual bool BuildCommand(const AgentSessionRequest& request, AgentCommand& outCommand, std::string& outError) = 0;
    // 起動直後の握手。
    virtual void OnStarted(AgentSession& session) = 0;
    virtual bool Send(AgentSession& session, const AgentMessage& message, std::string& outError) = 0;
    virtual void Interrupt(AgentSession& session) = 0;
    virtual void HandleLine(AgentSession& session, const nlohmann::json& line) = 0;
    virtual void Respond(AgentSession& session, const AgentToolApproval& approval, bool allow) = 0;
};

std::unique_ptr<AgentProtocol> CreateClaudeProtocol();
std::unique_ptr<AgentProtocol> CreateCodexProtocol();

// 長い文は UTF-8 の文字の途中で切らずに縮める。
std::string ClipAgentText(std::string text);
// 文字列はそのまま、{type: text, text} の配列は本文をつなぎ、それ以外は JSON の文字列。
std::string AgentJsonText(const nlohmann::json& value);
// / 区切りの UTF-8 パス（TOML・JSON にそのまま書ける）。
std::string AgentGenericPath(const std::filesystem::path& path);
// 実行ファイルを command に入れる。npm の .cmd は node と同梱の .js を直接起動する（cmd.exe を通すと引数の引用が崩れる）。
bool ResolveAgentLauncher(const std::filesystem::path& executable, AgentCommand& inOutCommand, std::string& outError);

} // namespace HIKARI::EDITOR::AGENT
