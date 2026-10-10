#include "Editor/Play/HIKARI_BackgroundPlayRun.h"

#include <fstream>
#include <system_error>

#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Projects/HIKARI_EditorChildProcess.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"

namespace HIKARI::EDITOR {
namespace {

bool WriteJson(const std::filesystem::path& path, const nlohmann::json& value) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << value.dump(2);
    return static_cast<bool>(output);
}

nlohmann::json Pending(uint64_t runId) {
    return {{"runId", runId},
            {"pending", true},
            {"poll", {{"operation", "play.background.status"}, {"parameters", nlohmann::json::object()}}}};
}

} // namespace

BackgroundPlayRun::BackgroundPlayRun() = default;

BackgroundPlayRun::~BackgroundPlayRun() = default;

EditorOperationResult BackgroundPlayRun::Start(const Request& request) {
    if (process_ && process_->Running())
        return OperationFailure("BACKGROUND_PLAY_BUSY", "A background Play run is still running; poll its status.");
    if (request.scene == nullptr || request.sceneGuid.empty())
        return OperationFailure("SCENE_NOT_SAVED",
                                "Background Play needs a scene that has been saved as an asset once.");
    if (request.frames == 0u || request.frames > 10000u)
        return OperationFailure("INVALID_FRAMES", "frames must be 1 to 10000.");
    const std::vector<uint32_t>& captures = request.captureFrames;
    uint32_t stride = 1u;
    if (captures.size() > 1u) {
        stride = captures[1] > captures[0] ? captures[1] - captures[0] : 0u;
        for (size_t index = 1u; index < captures.size(); ++index) {
            if (stride == 0u || captures[index] != captures[0] + static_cast<uint32_t>(index) * stride)
                return OperationFailure("INVALID_CAPTURE", "captureAt must be increasing and evenly spaced, e.g. "
                                                           "[120, 240, 360].");
        }
    }
    if (!captures.empty() && captures.back() > request.frames)
        return OperationFailure("INVALID_CAPTURE", "captureAt frames must be within frames.");

    runId_ = nextId_++;
    finished_.reset();
    folder_ = request.libraryRoot / "EditorAI" / "BackgroundPlay" / std::to_string(runId_);
    std::error_code error;
    std::filesystem::remove_all(folder_, error);
    std::filesystem::create_directories(folder_, error);
    // 保存前の場面をそのまま写す。身元（GUID）は元の資産のまま、内容だけこの文件から読ませる。
    const std::filesystem::path scene = folder_ / "scene.json";
    if (!SceneSerializer{}.SaveToFile(TEXT::PathToUtf8(scene), *request.scene))
        return OperationFailure("SNAPSHOT_FAILED", "Could not write the scene snapshot.");
    nlohmann::json runtime = AGENT::HeadlessRuntimeSettings(!request.projectQuality);
    runtime["startupSceneGuid"] = request.sceneGuid;
    runtime["startupSceneSnapshot"] = TEXT::PathToUtf8(scene);
    const nlohmann::json config{{"runtime", std::move(runtime)}};
    run_ = {};
    run_.descriptor = request.descriptor;
    run_.frames = request.frames;
    run_.play = true;
    run_.projectModule = request.projectModule;
    run_.runtimeConfig = folder_ / "runtime.json";
    if (!WriteJson(run_.runtimeConfig, config))
        return OperationFailure("SNAPSHOT_FAILED", "Could not write the runtime configuration.");
    if (request.track.is_object()) {
        run_.inputReplay = folder_ / "track.hinputtrack";
        if (!WriteJson(run_.inputReplay, request.track))
            return OperationFailure("SNAPSHOT_FAILED", "Could not write the input track.");
    } else {
        run_.inputReplay = request.trackFile;
    }
    if (!captures.empty()) {
        run_.captureOutput = folder_ / "capture.png";
        run_.captureFrame = captures.front();
        run_.captureCount = static_cast<uint32_t>(captures.size());
        run_.captureStride = stride;
    }
    logs_ = request.libraryRoot / "Logs";
    logsBefore_ = AGENT::ListLogFiles(logs_);
    auto process = std::make_unique<EditorChildProcess>();
    std::string message;
    if (!process->Start(request.engine, AGENT::BuildHeadlessEngineArguments(run_), request.descriptor.parent_path(),
                        folder_ / "engine.out", message))
        return OperationFailure("LAUNCH_FAILED", message);
    process_ = std::move(process);
    started_ = std::chrono::steady_clock::now();
    // 無頭は毎秒百フレーム前後。起動と资产の読み込みの時間を足して待つ。
    timeout_ = std::chrono::seconds(request.frames / 20u + 180u);
    return OperationOk("Background Play started; the editor stays free.", Pending(runId_));
}

nlohmann::json BackgroundPlayRun::Report(uint32_t exitCode) const {
    nlohmann::json data{{"runId", runId_},
                        {"exitCode", exitCode},
                        {"frames", run_.frames},
                        {"captures", AGENT::CollectHeadlessCaptures(run_)}};
    if (const std::filesystem::path log = AGENT::FindNewLogFile(logs_, logsBefore_); !log.empty()) {
        data["log"] = TEXT::PathToUtf8(log);
        data["problems"] = AGENT::ReadLogProblems(log);
    }
    return data;
}

EditorOperationResult BackgroundPlayRun::Status() {
    if (finished_)
        return *finished_;
    if (!process_)
        return OperationFailure("NO_BACKGROUND_PLAY", "No background Play run has been started.");
    if (process_->Running()) {
        if (std::chrono::steady_clock::now() - started_ < timeout_) {
            nlohmann::json data = Pending(runId_);
            data["seconds"] =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started_).count();
            return OperationOk("Background Play is running.", std::move(data));
        }
        process_->Cancel();
        process_.reset();
        // 止めるまでに撮れた截图と日志は返す（どこまで進んだかの手掛かり）。
        finished_ = OperationFailure("TIMEOUT", "The background engine did not finish in time and was stopped; run "
                                                "fewer frames (about 60 frames per second of game time).");
        finished_->data = Report(0u);
        return *finished_;
    }
    const uint32_t exitCode = process_->ExitCode();
    process_.reset();
    nlohmann::json data = Report(exitCode);
    finished_ = exitCode == 0u
                    ? OperationOk("Background Play finished.", std::move(data))
                    : OperationFailure("ENGINE_FAILED", "The background engine exited with code " +
                                                            std::to_string(exitCode) + "; see log and problems.");
    if (exitCode != 0u)
        finished_->data = std::move(data);
    return *finished_;
}

} // namespace HIKARI::EDITOR
