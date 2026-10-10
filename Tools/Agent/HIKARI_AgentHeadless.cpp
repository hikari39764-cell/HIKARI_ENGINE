#include "HIKARI_AgentHeadless.h"

#include "Editor/Agent/HIKARI_AgentConnection.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace HIKARI::AGENT_CLI {
namespace {

constexpr size_t kMaximumProblems = 40u;

std::string ToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

nlohmann::json Failure(const std::string& code, const std::string& message) {
    return {{"ok", false}, {"code", code}, {"message", message}};
}

std::filesystem::path ExecutableDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

// CreateProcessW の命令行の一引数（空白と引用符を含む場合）。
std::wstring Quote(const std::wstring& argument) {
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        quoted.append(character == L'"' ? backslashes * 2u + 1u : backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(character);
    }
    quoted.append(backslashes * 2u, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

std::filesystem::path FindDescriptor(const std::filesystem::path& projectRoot) {
    std::error_code error;
    if (std::filesystem::is_regular_file(projectRoot / "Project.hikari", error))
        return projectRoot / "Project.hikari";
    for (const auto& entry : std::filesystem::directory_iterator(projectRoot, error)) {
        if (entry.path().extension() == ".hikari")
            return entry.path();
    }
    return {};
}

std::filesystem::path NewestLog(const std::filesystem::path& folder, std::filesystem::file_time_type after) {
    std::filesystem::path newest;
    std::filesystem::file_time_type newestTime = after;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        const auto time = entry.last_write_time(error);
        if (entry.path().extension() == ".log" && !error && time >= newestTime) {
            newest = entry.path();
            newestTime = time;
        }
    }
    return newest;
}

nlohmann::json ReadProblems(const std::filesystem::path& log) {
    nlohmann::json problems = nlohmann::json::array();
    std::ifstream input(log, std::ios::binary);
    for (std::string line; std::getline(input, line) && problems.size() < kMaximumProblems;) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.find("[WARN]") != std::string::npos || line.find("[ERROR]") != std::string::npos)
            problems.push_back(line);
    }
    return problems;
}

} // namespace

nlohmann::json RunHeadless(const HeadlessRunOptions& options) {
    const std::filesystem::path engine = ExecutableDirectory() / "Runtime" / "HIKARI_Engine.exe";
    std::error_code error;
    if (!std::filesystem::is_regular_file(engine, error))
        return Failure("ENGINE_NOT_FOUND", "找不到引擎：" + ToUtf8(engine));
    const std::filesystem::path descriptor = FindDescriptor(options.projectRoot);
    if (descriptor.empty())
        return Failure("PROJECT_NOT_FOUND", "项目目录里没有 .hikari 描述文件：" + ToUtf8(options.projectRoot));
    if (options.frames == 0u || options.frames > 10000u)
        return Failure("INVALID_FRAMES", "--frames 必须在 1 到 10000 之间");

    std::vector<std::wstring> arguments = {engine.wstring(),      L"--project",
                                           descriptor.wstring(),  L"--headless-render",
                                           L"--exit-after-frame", std::to_wstring(options.frames)};
    if (options.play || options.inputReplay)
        arguments.push_back(L"--play-on-start");
    if (options.inputReplay) {
        arguments.push_back(L"--input-replay");
        arguments.push_back(std::filesystem::absolute(*options.inputReplay, error).wstring());
    }
    std::filesystem::path capture;
    if (options.captureFrame) {
        const std::filesystem::path fallback =
            EDITOR::AGENT::FindProjectLibraryRoot(options.projectRoot) / "EditorAI" / "Captures" / "headless.png";
        capture = std::filesystem::absolute(options.captureOutput.empty() ? fallback : options.captureOutput, error);
        std::filesystem::create_directories(capture.parent_path(), error);
        std::filesystem::remove(capture, error);
        arguments.insert(arguments.end(), {L"--capture-output", capture.wstring(), L"--capture-frame",
                                           std::to_wstring(*options.captureFrame)});
    }
    if (options.allowMissingModule)
        arguments.push_back(L"--allow-missing-project-module");
    if (!options.runtimeConfig.empty()) {
        // 相対パスは引擎が見つけられず黙って既定に戻るので、絶対パスで渡す。
        arguments.push_back(L"--runtime-config");
        arguments.push_back(std::filesystem::absolute(options.runtimeConfig, error).wstring());
    }
    std::wstring commandLine;
    for (const std::wstring& argument : arguments)
        commandLine += (commandLine.empty() ? L"" : L" ") + Quote(argument);

    const std::filesystem::path logs = EDITOR::AGENT::FindProjectLibraryRoot(options.projectRoot) / "Logs";
    const auto started = std::filesystem::file_time_type::clock::now();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring workingDirectory = options.projectRoot.wstring();
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, BELOW_NORMAL_PRIORITY_CLASS, nullptr,
                        workingDirectory.c_str(), &startup, &process))
        return Failure("LAUNCH_FAILED", "无法启动引擎（Win32 error " + std::to_string(GetLastError()) + "）");
    CloseHandle(process.hThread);
    // 無頭は毎秒百フレーム前後。起動と導入の時間を足して待つ。
    const DWORD timeout = (options.frames / 20u + 180u) * 1000u;
    const DWORD wait = WaitForSingleObject(process.hProcess, timeout);
    DWORD exitCode = 0;
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hProcess);
        return Failure("TIMEOUT", "引擎在 " + std::to_string(timeout / 1000u) + " 秒内没有结束，已终止");
    }
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);

    nlohmann::json data{{"exitCode", exitCode}, {"frames", options.frames}, {"captures", nlohmann::json::array()}};
    if (!capture.empty() && std::filesystem::is_regular_file(capture, error))
        data["captures"].push_back({{"frame", *options.captureFrame}, {"path", ToUtf8(capture)}});
    const std::filesystem::path log = NewestLog(logs, started - std::chrono::seconds(2));
    if (!log.empty()) {
        data["log"] = ToUtf8(log);
        data["problems"] = ReadProblems(log);
    }
    const bool ok = exitCode == 0u;
    return {{"ok", ok},
            {"code", ok ? "OK" : "ENGINE_FAILED"},
            {"message", ok ? "无头运行结束。" : "引擎以非零退出码结束，查看 log 与 problems。"},
            {"data", std::move(data)}};
}

} // namespace HIKARI::AGENT_CLI
