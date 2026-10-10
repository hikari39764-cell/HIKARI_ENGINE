#include "Editor/Agent/HIKARI_HeadlessEngineRun.h"

#include <fstream>
#include <system_error>

namespace HIKARI::EDITOR::AGENT {
namespace {

std::filesystem::path Absolute(const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return error ? path : absolute;
}

// 起動・资产の読み込み・Play の開始に使うフレームの余裕。
constexpr uint32_t kStartupFrames = 2400u;

} // namespace

std::vector<std::wstring> BuildHeadlessEngineArguments(const HeadlessEngineRun& run) {
    // 結果を書いたら起動した側が止める。--exit-after-frame は戻ってこない時の上限。
    std::vector<std::wstring> arguments = {L"--project",
                                           run.descriptor.wstring(),
                                           L"--headless-render",
                                           L"--exit-after-frame",
                                           std::to_wstring(run.ticks + kStartupFrames),
                                           L"--agent-bridge-off",
                                           L"--agent-play-run",
                                           Absolute(run.request).wstring()};
    if (run.projectModule.empty())
        arguments.push_back(L"--allow-missing-project-module");
    else
        arguments.insert(arguments.end(), {L"--project-module", Absolute(run.projectModule).wstring()});
    // 相対パスは引擎が見つけられず黙って既定に戻るので、絶対パスで渡す。
    if (!run.runtimeConfig.empty())
        arguments.insert(arguments.end(), {L"--runtime-config", Absolute(run.runtimeConfig).wstring()});
    return arguments;
}

nlohmann::json HeadlessRuntimeSettings(bool light) {
    // play.run は编辑器の制御器が回すので、编辑器の界面は残す（重いのは窗口の大きさと画質）。
    nlohmann::json runtime{{"windowWidth", light ? 1280 : 1920}, {"windowHeight", light ? 720 : 1080}};
    if (light)
        runtime["renderQuality"] = {{"sceneResolution", "720p"},       {"dynamicGiQuality", "Low"},
                                    {"localShadowQuality", "Low"},     {"volumetricLightingQuality", "Low"},
                                    {"volumetricCloudQuality", "Low"}, {"depthOfFieldQuality", "Low"},
                                    {"motionBlurQuality", "Off"}};
    return runtime;
}

nlohmann::json ReadHeadlessPlayResult(const HeadlessEngineRun& run) {
    std::ifstream input(run.request.parent_path() / "result.json", std::ios::binary);
    if (!input)
        return nullptr;
    nlohmann::json result = nlohmann::json::parse(input, nullptr, false);
    return result.is_discarded() ? nullptr : result;
}

uint32_t HeadlessRunTimeoutSeconds(uint32_t ticks) { return ticks / 20u + 180u; }

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
