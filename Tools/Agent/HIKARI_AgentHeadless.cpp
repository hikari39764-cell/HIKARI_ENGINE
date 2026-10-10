#include "HIKARI_AgentHeadless.h"

#include "Editor/Agent/HIKARI_AgentConnection.h"
#include "Editor/Agent/HIKARI_HeadlessEngineRun.h"

#include <fstream>
#include <set>
#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace HIKARI::AGENT_CLI {
namespace {

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

} // namespace

nlohmann::json RunHeadless(const HeadlessRunOptions& options) {
    const std::filesystem::path engine = ExecutableDirectory() / "Runtime" / "HIKARI_Engine.exe";
    std::error_code error;
    if (!std::filesystem::is_regular_file(engine, error))
        return Failure("ENGINE_NOT_FOUND", "找不到引擎：" + ToUtf8(engine));
    EDITOR::AGENT::HeadlessEngineRun run;
    run.descriptor = EDITOR::AGENT::FindProjectDescriptor(options.projectRoot);
    if (run.descriptor.empty())
        return Failure("PROJECT_NOT_FOUND", "项目目录里没有 .hikari 描述文件：" + ToUtf8(options.projectRoot));
    if (options.frames == 0u || options.frames > 10000u)
        return Failure("INVALID_FRAMES", "--frames 必须在 1 到 10000 之间");
    const std::filesystem::path library = EDITOR::AGENT::FindProjectLibraryRoot(options.projectRoot);
    run.frames = options.frames;
    run.play = options.play;
    run.inputReplay = options.inputReplay.value_or(std::filesystem::path{});
    run.runtimeConfig = options.runtimeConfig;
    // 運行設定が無ければ、後台試玩と同じ軽い設定で描く。
    if (run.runtimeConfig.empty()) {
        run.runtimeConfig = library / "EditorAI" / "Headless" / "runtime.json";
        std::filesystem::create_directories(run.runtimeConfig.parent_path(), error);
        std::ofstream output(run.runtimeConfig, std::ios::binary | std::ios::trunc);
        output << nlohmann::json{{"runtime", EDITOR::AGENT::HeadlessRuntimeSettings(true)}}.dump(2);
        if (!output)
            return Failure("CONFIG_FAILED", "无法写入运行配置：" + ToUtf8(run.runtimeConfig));
    }
    // Launcher と同じく、項目が構築済みの模块を使う。無ければ模块なしで動かす。
    const std::filesystem::path module = library / "Build" / "editor-release" / "bin" / "HIKARI_GameProject.dll";
    if (!options.allowMissingModule && std::filesystem::is_regular_file(module, error))
        run.projectModule = module;
    if (options.captureFrame) {
        run.captureOutput =
            options.captureOutput.empty() ? library / "EditorAI" / "Captures" / "headless.png" : options.captureOutput;
        run.captureFrame = *options.captureFrame;
        std::filesystem::create_directories(std::filesystem::absolute(run.captureOutput, error).parent_path(), error);
        std::filesystem::remove(run.captureOutput, error);
    }
    std::wstring commandLine = Quote(engine.wstring());
    for (const std::wstring& argument : EDITOR::AGENT::BuildHeadlessEngineArguments(run))
        commandLine += L" " + Quote(argument);

    const std::filesystem::path logs = library / "Logs";
    const std::set<std::filesystem::path> logsBefore = EDITOR::AGENT::ListLogFiles(logs);
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

    nlohmann::json data{
        {"exitCode", exitCode}, {"frames", options.frames}, {"captures", EDITOR::AGENT::CollectHeadlessCaptures(run)}};
    const std::filesystem::path log = EDITOR::AGENT::FindNewLogFile(logs, logsBefore);
    if (!log.empty()) {
        data["log"] = ToUtf8(log);
        data["problems"] = EDITOR::AGENT::ReadLogProblems(log);
    }
    const bool ok = exitCode == 0u;
    return {{"ok", ok},
            {"code", ok ? "OK" : "ENGINE_FAILED"},
            {"message", ok ? "无头运行结束。" : "引擎以非零退出码结束，查看 log 与 problems。"},
            {"data", std::move(data)}};
}

} // namespace HIKARI::AGENT_CLI
