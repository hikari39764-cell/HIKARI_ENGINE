#include "Editor/Agent/HIKARI_AgentSession.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Agent/HIKARI_AgentProtocol.h"
#include "Editor/Projects/HIKARI_EditorChildProcess.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace HIKARI::EDITOR::AGENT {
namespace {

constexpr size_t kMaximumEventText = 1200u;
constexpr size_t kMaximumEvents = 4000u;

std::filesystem::path KnownFolder(const wchar_t* variable) {
    wchar_t value[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(variable, value, MAX_PATH);
    return length > 0u && length < MAX_PATH ? std::filesystem::path(value) : std::filesystem::path{};
}

std::filesystem::path FindOnPath(const wchar_t* name) {
    for (const wchar_t* extension : {L".exe", L".cmd"}) {
        wchar_t found[MAX_PATH]{};
        if (SearchPathW(nullptr, name, extension, MAX_PATH, found, nullptr) != 0)
            return found;
    }
    return {};
}

// "2.1.295" の大小。数字以外の区切りは無視する。
bool NewerVersion(const std::string& lhs, const std::string& rhs) {
    const auto parts = [](const std::string& text) {
        std::vector<int> numbers;
        int value = 0;
        bool digit = false;
        for (const char c : text) {
            if (c >= '0' && c <= '9') {
                value = value * 10 + (c - '0');
                digit = true;
            } else if (digit) {
                numbers.push_back(value);
                value = 0;
                digit = false;
            }
        }
        if (digit)
            numbers.push_back(value);
        return numbers;
    };
    return parts(lhs) > parts(rhs);
}

// Claude 桌面版は %APPDATA%\Claude\claude-code\<版本>\<识别码>\claude.exe に同梱する。最も新しい版を使う。
std::filesystem::path FindBundledClaude() {
    const std::filesystem::path root = KnownFolder(L"APPDATA") / "Claude" / "claude-code";
    std::error_code error;
    std::filesystem::path best;
    std::string bestVersion;
    for (const auto& version : std::filesystem::directory_iterator(root, error)) {
        if (!version.is_directory(error))
            continue;
        const std::string name = TEXT::PathToUtf8(version.path().filename());
        if (!best.empty() && !NewerVersion(name, bestVersion))
            continue;
        for (const auto& build : std::filesystem::directory_iterator(version.path(), error)) {
            const std::filesystem::path candidate = build.path() / "claude.exe";
            if (std::filesystem::is_regular_file(candidate, error)) {
                best = candidate;
                bestVersion = name;
                break;
            }
        }
    }
    return best;
}

bool IsFile(const std::filesystem::path& path) {
    std::error_code error;
    return !path.empty() && std::filesystem::is_regular_file(path, error);
}

} // namespace

std::filesystem::path FindAgentExecutable(AgentKind kind) {
    const bool claude = kind == AgentKind::ClaudeCode;
    if (std::filesystem::path found = FindOnPath(claude ? L"claude" : L"codex"); !found.empty())
        return found;
    const std::filesystem::path home = KnownFolder(L"USERPROFILE");
    const std::filesystem::path npm = KnownFolder(L"APPDATA") / "npm" / (claude ? "claude.cmd" : "codex.cmd");
    if (claude) {
        for (const std::filesystem::path& candidate : {home / ".local" / "bin" / "claude.exe", npm})
            if (IsFile(candidate))
                return candidate;
        return FindBundledClaude();
    }
    // Codex 应用は app-server の実行ファイルを ~/.codex/plugins/.plugin-appserver に置く。
    for (const std::filesystem::path& candidate :
         {npm, home / ".codex" / "plugins" / ".plugin-appserver" / "codex.exe"})
        if (IsFile(candidate))
            return candidate;
    return {};
}

std::string ClipAgentText(std::string text) {
    if (text.size() <= kMaximumEventText)
        return text;
    size_t end = kMaximumEventText;
    while (end > 0u && (static_cast<unsigned char>(text[end]) & 0xC0u) == 0x80u)
        --end;
    text.resize(end);
    return text + "…";
}

std::string AgentJsonText(const nlohmann::json& value) {
    if (value.is_string())
        return value.get<std::string>();
    if (value.is_array()) {
        std::string joined;
        for (const nlohmann::json& part : value) {
            if (part.is_object() && part.value("type", std::string{}) == "text")
                joined += (joined.empty() ? "" : "\n") + part.value("text", std::string{});
        }
        if (!joined.empty())
            return joined;
    }
    return value.is_null() ? std::string{} : value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

std::string AgentGenericPath(const std::filesystem::path& path) { return TEXT::PathToUtf8(path.generic_wstring()); }

bool ResolveAgentLauncher(const std::filesystem::path& executable, AgentCommand& inOutCommand, std::string& outError) {
    const std::wstring extension = executable.extension().wstring();
    // npm は .ps1 の起動脚本も作る。PowerShell で直接起動する。
    if (_wcsicmp(extension.c_str(), L".ps1") == 0) {
        inOutCommand.executable = EditorChildProcess::PowerShell();
        inOutCommand.arguments.insert(inOutCommand.arguments.begin(),
                                      {L"-NoProfile", L"-ExecutionPolicy", L"Bypass", L"-File", executable.wstring()});
        return true;
    }
    if (_wcsicmp(extension.c_str(), L".cmd") != 0 && _wcsicmp(extension.c_str(), L".bat") != 0) {
        inOutCommand.executable = executable;
        return true;
    }
    std::ifstream input(executable, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    for (const std::string_view marker : {std::string_view("\"%~dp0\\"), std::string_view("\"%dp0%\\")}) {
        size_t begin = 0u;
        while ((begin = text.find(marker, begin)) != std::string::npos) {
            begin += marker.size();
            const size_t end = text.find('"', begin);
            if (end == std::string::npos)
                break;
            const std::string relative = text.substr(begin, end - begin);
            if (relative.ends_with(".js") || relative.ends_with(".cjs") || relative.ends_with(".mjs")) {
                std::filesystem::path node = executable.parent_path() / "node.exe";
                if (!IsFile(node))
                    node = FindOnPath(L"node");
                if (node.empty())
                    break;
                inOutCommand.executable = node;
                inOutCommand.arguments.insert(inOutCommand.arguments.begin(),
                                              (executable.parent_path() / TEXT::PathFromUtf8(relative)).wstring());
                return true;
            }
            begin = end;
        }
    }
    outError = "Cannot resolve the npm launcher " + TEXT::PathToUtf8(executable) +
               "; install the native executable or put node.exe on PATH.";
    return false;
}

AgentSession::AgentSession() = default;

AgentSession::~AgentSession() { Stop(); }

bool AgentSession::Start(const AgentSessionRequest& request, std::string& outError) {
    Stop();
    if (request.executable.empty()) {
        outError = std::string(TEXT::EnumToName(kAgentKindNames, request.kind)) + " was not found.";
        return false;
    }
    if (request.hikariExecutable.empty()) {
        outError = "hikari.exe was not found next to the editor.";
        return false;
    }
    request_ = request;
    protocol_ = request.kind == AgentKind::ClaudeCode ? CreateClaudeProtocol() : CreateCodexProtocol();
    std::error_code error;
    std::filesystem::create_directories(request.runFolder, error);
    AgentCommand command;
    if (!protocol_->BuildCommand(request, command, outError))
        return false;
    output_ = request.runFolder / "output.jsonl";
    readOffset_ = 0u;
    partialLine_.clear();
    events_.clear();
    approvals_.clear();
    turnGrants_.clear();
    usage_ = {};
    sessionId_ = request.resumeSession;
    busy_ = false;
    exited_ = false;
    process_ = std::make_unique<EditorChildProcess>();
    EditorChildProcessOptions options;
    options.writableInput = true;
    options.errorLog = request.runFolder / "stderr.log";
    if (!process_->Start(command.executable, command.arguments, request.projectRoot, output_, outError, options)) {
        process_.reset();
        return false;
    }
    protocol_->OnStarted(*this);
    return true;
}

bool AgentSession::Send(const AgentMessage& message, std::string& outError) {
    if (!Running()) {
        outError = "The agent is not running.";
        return false;
    }
    if (busy_) {
        outError = "The agent is still answering.";
        return false;
    }
    if (!protocol_->Send(*this, message, outError))
        return false;
    AddEvent({AgentEvent::Kind::User, {}, ClipAgentText(message.text)});
    SetBusy(true);
    return true;
}

void AgentSession::Interrupt() {
    if (Running() && busy_)
        protocol_->Interrupt(*this);
}

bool AgentSession::WriteLine(const nlohmann::json& message) {
    return process_ &&
           process_->WriteInput(message.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n");
}

void AgentSession::AddEvent(AgentEvent event) {
    if (events_.size() < kMaximumEvents)
        events_.push_back(std::move(event));
}

bool AgentSession::AddApproval(AgentToolApproval approval) {
    if (!approval.grantKey.empty() && turnGrants_.contains(approval.grantKey))
        return true;
    approval.id = nextApprovalId_++;
    approvals_.push_back(std::move(approval));
    return false;
}

void AgentSession::RemoveApproval(std::string_view key, const nlohmann::json& value) {
    std::erase_if(approvals_, [key, &value](const AgentToolApproval& approval) {
        const auto found = approval.token.find(key);
        return found != approval.token.end() && *found == value;
    });
}

void AgentSession::ResolveApproval(uint64_t id, AgentToolDecision decision) {
    const auto found = std::find_if(approvals_.begin(), approvals_.end(),
                                    [id](const AgentToolApproval& approval) { return approval.id == id; });
    if (found == approvals_.end())
        return;
    const AgentToolApproval approval = *found;
    approvals_.erase(found);
    if (decision == AgentToolDecision::AllowTurn && !approval.grantKey.empty())
        turnGrants_.insert(approval.grantKey);
    if (Running())
        protocol_->Respond(*this, approval, decision != AgentToolDecision::Deny);
}

void AgentSession::SetBusy(bool busy) {
    busy_ = busy;
    if (!busy) {
        turnGrants_.clear();
        approvals_.clear();
    }
}

void AgentSession::ReadNewOutput() {
    std::ifstream input(output_, std::ios::binary);
    if (!input)
        return;
    input.seekg(static_cast<std::streamoff>(readOffset_));
    std::string chunk((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    readOffset_ += chunk.size();
    partialLine_ += chunk;
    size_t begin = 0u;
    for (size_t end = partialLine_.find('\n'); end != std::string::npos; end = partialLine_.find('\n', begin)) {
        std::string_view line = std::string_view(partialLine_).substr(begin, end - begin);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.remove_suffix(1);
        begin = end + 1u;
        if (line.empty())
            continue;
        const nlohmann::json value = nlohmann::json::parse(line.begin(), line.end(), nullptr, false);
        if (value.is_object())
            protocol_->HandleLine(*this, value);
        else
            AddEvent({AgentEvent::Kind::Raw, {}, ClipAgentText(std::string(line))});
    }
    partialLine_.erase(0u, begin);
}

void AgentSession::Poll() {
    if (!process_ || exited_)
        return;
    ReadNewOutput();
    if (process_->Running())
        return;
    ReadNewOutput();
    exited_ = true;
    const uint32_t code = process_->ExitCode();
    if (code != 0u)
        AddEvent({AgentEvent::Kind::Error, {}, "The agent exited with code " + std::to_string(code) + ".", true});
    SetBusy(false);
    process_.reset();
}

void AgentSession::Stop() noexcept {
    if (process_) {
        process_->CloseInput();
        process_->Cancel();
        process_->Wait(2000u);
        process_.reset();
    }
    exited_ = true;
    busy_ = false;
    approvals_.clear();
    turnGrants_.clear();
}

bool AgentSession::Running() const noexcept { return process_ != nullptr && !exited_; }

} // namespace HIKARI::EDITOR::AGENT
