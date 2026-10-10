#include "HIKARI_AgentHeadless.h"

#include "Editor/Agent/HIKARI_AgentConnection.h"
#include "Editor/Agent/HIKARI_HeadlessEngineRun.h"

#include <chrono>
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

bool WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << value.dump(2);
    return static_cast<bool>(output);
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
    run.ticks = options.run.value("ticks", 600u);
    if (run.ticks == 0u || run.ticks > EDITOR::AGENT::kMaximumHeadlessTicks)
        return Failure("INVALID_TICKS",
                       "tick 数必须在 1 到 " + std::to_string(EDITOR::AGENT::kMaximumHeadlessTicks) + " 之间");
    const std::filesystem::path library = EDITOR::AGENT::FindProjectLibraryRoot(options.projectRoot);
    const std::filesystem::path folder = library / "EditorAI" / "Headless";
    std::filesystem::remove_all(folder, error);
    std::filesystem::create_directories(folder, error);
    nlohmann::json request = options.run;
    request["ticks"] = run.ticks;
    request["pauseAfter"] = false;
    run.request = folder / "request.json";
    run.runtimeConfig = folder / "runtime.json";
    if (!WriteJson(run.request, request) ||
        !WriteJson(run.runtimeConfig, {{"runtime", EDITOR::AGENT::HeadlessRuntimeSettings(!options.projectQuality)}}))
        return Failure("CONFIG_FAILED", "无法写入运行文件：" + ToUtf8(folder));
    // Launcher と同じく、項目が構築済みの模块を使う。無ければ模块なしで動かす。
    const std::filesystem::path module = library / "Build" / "editor-release" / "bin" / "HIKARI_GameProject.dll";
    if (!options.allowMissingModule && std::filesystem::is_regular_file(module, error))
        run.projectModule = module;
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
    // 後ろの编辑器は結果を書いたら用済み。終了を待たずに止める。
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(EDITOR::AGENT::HeadlessRunTimeoutSeconds(run.ticks));
    nlohmann::json result = nullptr;
    bool exited = false;
    while (!(result = EDITOR::AGENT::ReadHeadlessPlayResult(run)).is_object()) {
        if (WaitForSingleObject(process.hProcess, 500) == WAIT_OBJECT_0) {
            // 終わる直前に書いた結果を拾う。
            result = EDITOR::AGENT::ReadHeadlessPlayResult(run);
            exited = true;
            break;
        }
        if (std::chrono::steady_clock::now() > deadline)
            break;
    }
    DWORD exitCode = 0;
    if (!exited)
        TerminateProcess(process.hProcess, 0);
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    if (result.is_object())
        return result;

    nlohmann::json data{{"exitCode", exitCode}};
    if (const std::filesystem::path log = EDITOR::AGENT::FindNewLogFile(logs, logsBefore); !log.empty()) {
        data["log"] = ToUtf8(log);
        data["problems"] = EDITOR::AGENT::ReadLogProblems(log);
    }
    nlohmann::json failure = exited ? Failure("ENGINE_FAILED", "引擎没有给出结果就结束了，查看 log 与 problems。")
                                    : Failure("TIMEOUT", "引擎没有按时给出结果，已终止；请减少 tick 数。");
    failure["data"] = std::move(data);
    return failure;
}

} // namespace HIKARI::AGENT_CLI
