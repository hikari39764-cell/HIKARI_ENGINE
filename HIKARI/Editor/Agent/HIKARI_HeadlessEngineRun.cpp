#include "Editor/Agent/HIKARI_HeadlessEngineRun.h"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace HIKARI::EDITOR::AGENT {
namespace {

std::string ToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::filesystem::path Absolute(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return error ? path : absolute;
}

} // namespace

std::vector<std::wstring> BuildHeadlessEngineArguments(const HeadlessEngineRun& run) {
    std::vector<std::wstring> arguments = {L"--project",          run.descriptor.wstring(),    L"--headless-render",
                                           L"--exit-after-frame", std::to_wstring(run.frames), L"--agent-bridge-off"};
    if (run.play || !run.inputReplay.empty())
        arguments.push_back(L"--play-on-start");
    if (!run.inputReplay.empty())
        arguments.insert(arguments.end(), {L"--input-replay", Absolute(run.inputReplay).wstring()});
    if (!run.captureOutput.empty()) {
        arguments.insert(arguments.end(), {L"--capture-output", Absolute(run.captureOutput).wstring(),
                                           L"--capture-frame", std::to_wstring(run.captureFrame)});
        if (run.captureCount > 1u)
            arguments.insert(arguments.end(), {L"--capture-count", std::to_wstring(run.captureCount),
                                               L"--capture-stride", std::to_wstring(run.captureStride)});
    }
    if (run.projectModule.empty())
        arguments.push_back(L"--allow-missing-project-module");
    else
        arguments.insert(arguments.end(), {L"--project-module", Absolute(run.projectModule).wstring()});
    // 相対パスは引擎が見つけられず黙って既定に戻るので、絶対パスで渡す。
    if (!run.runtimeConfig.empty())
        arguments.insert(arguments.end(), {L"--runtime-config", Absolute(run.runtimeConfig).wstring()});
    return arguments;
}

nlohmann::json CollectHeadlessCaptures(const HeadlessEngineRun& run) {
    nlohmann::json captures = nlohmann::json::array();
    if (run.captureOutput.empty())
        return captures;
    std::error_code error;
    const std::filesystem::path output = Absolute(run.captureOutput);
    for (uint32_t index = 0u; index < (std::max)(run.captureCount, 1u); ++index) {
        const uint32_t frame = run.captureFrame + index * run.captureStride;
        std::filesystem::path path = output;
        if (run.captureCount > 1u)
            path.replace_filename(output.stem().wstring() + L"_" + std::to_wstring(frame) +
                                  output.extension().wstring());
        if (std::filesystem::is_regular_file(path, error))
            captures.push_back({{"frame", frame}, {"path", ToUtf8(path)}});
    }
    return captures;
}

std::filesystem::path FindProjectDescriptor(const std::filesystem::path& projectRoot) {
    std::error_code error;
    if (std::filesystem::is_regular_file(projectRoot / "Project.hikari", error))
        return projectRoot / "Project.hikari";
    for (const auto& entry : std::filesystem::directory_iterator(projectRoot, error)) {
        if (entry.path().extension() == ".hikari")
            return entry.path();
    }
    return {};
}

std::set<std::filesystem::path> ListLogFiles(const std::filesystem::path& folder) {
    std::set<std::filesystem::path> logs;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        if (entry.path().extension() == ".log")
            logs.insert(entry.path());
    }
    return logs;
}

std::filesystem::path FindNewLogFile(const std::filesystem::path& folder,
                                     const std::set<std::filesystem::path>& before) {
    std::filesystem::path newest;
    std::filesystem::file_time_type newestTime{};
    std::error_code error;
    for (const std::filesystem::path& log : ListLogFiles(folder)) {
        if (before.contains(log))
            continue;
        const auto time = std::filesystem::last_write_time(log, error);
        if (!error && (newest.empty() || time > newestTime)) {
            newest = log;
            newestTime = time;
        }
    }
    return newest;
}

nlohmann::json ReadLogProblems(const std::filesystem::path& log, size_t maximum) {
    nlohmann::json problems = nlohmann::json::array();
    std::ifstream input(log, std::ios::binary);
    for (std::string line; std::getline(input, line) && problems.size() < maximum;) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.find("[WARN]") != std::string::npos || line.find("[ERROR]") != std::string::npos)
            problems.push_back(line);
    }
    return problems;
}

} // namespace HIKARI::EDITOR::AGENT
