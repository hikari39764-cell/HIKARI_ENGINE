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

    runId_ = nextId_++;
    finished_.reset();
    const std::filesystem::path folder = request.libraryRoot / "EditorAI" / "BackgroundPlay" / std::to_string(runId_);
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    std::filesystem::create_directories(folder, error);
    // 保存前の場面をそのまま写す。身元（GUID）は元の資産のまま、内容だけこの文件から読ませる。
    const std::filesystem::path scene = folder / "scene.json";
    if (!SceneSerializer{}.SaveToFile(TEXT::PathToUtf8(scene), *request.scene))
        return OperationFailure("SNAPSHOT_FAILED", "Could not write the scene snapshot.");
    nlohmann::json runtime = AGENT::HeadlessRuntimeSettings(!request.projectQuality);
    runtime["startupSceneGuid"] = request.sceneGuid;
    runtime["startupSceneSnapshot"] = TEXT::PathToUtf8(scene);
    run_ = {};
    run_.descriptor = request.descriptor;
    run_.request = folder / "request.json";
    run_.ticks = request.run.value("ticks", 600u);
    run_.projectModule = request.projectModule;
    run_.runtimeConfig = folder / "runtime.json";
    if (!WriteJson(run_.runtimeConfig, {{"runtime", std::move(runtime)}}) || !WriteJson(run_.request, request.run))
        return OperationFailure("SNAPSHOT_FAILED", "Could not write the background run files.");
    logs_ = request.libraryRoot / "Logs";
    logsBefore_ = AGENT::ListLogFiles(logs_);
    auto process = std::make_unique<EditorChildProcess>();
    std::string message;
    if (!process->Start(request.engine, AGENT::BuildHeadlessEngineArguments(run_), request.descriptor.parent_path(),
                        folder / "engine.out", message))
        return OperationFailure("LAUNCH_FAILED", message);
    process_ = std::move(process);
    started_ = std::chrono::steady_clock::now();
    timeout_ = std::chrono::seconds(AGENT::HeadlessRunTimeoutSeconds(run_.ticks, !request.projectQuality));
    return OperationOk("Background Play started; the editor stays free.", Pending(runId_));
}

EditorOperationResult BackgroundPlayRun::Failure(std::string code, std::string message, uint32_t exitCode) const {
    EditorOperationResult result = OperationFailure(std::move(code), std::move(message));
    result.data = {{"runId", runId_}, {"exitCode", exitCode}};
    if (const std::filesystem::path log = AGENT::FindNewLogFile(logs_, logsBefore_); !log.empty()) {
        result.data["log"] = TEXT::PathToUtf8(log);
        result.data["problems"] = AGENT::ReadLogProblems(log);
    }
    return result;
}

EditorOperationResult BackgroundPlayRun::Status() {
    if (finished_)
        return *finished_;
    if (!process_)
        return OperationFailure("NO_BACKGROUND_PLAY", "No background Play run has been started.");
    // 後ろの编辑器は結果を書いたら用済み。終了を待たずに止める。
    if (nlohmann::json written = AGENT::ReadHeadlessPlayResult(run_); written.is_object()) {
        process_->Cancel();
        process_.reset();
        EditorOperationResult result{written.value("ok", false), written.value("code", std::string("RUN_FAILED")),
                                     written.value("message", std::string{}),
                                     written.value("data", nlohmann::json::object())};
        result.data["runId"] = runId_;
        finished_ = std::move(result);
        return *finished_;
    }
    if (process_->Running()) {
        if (std::chrono::steady_clock::now() - started_ < timeout_) {
            nlohmann::json data = Pending(runId_);
            data["seconds"] =
                std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started_).count();
            return OperationOk("Background Play is running.", std::move(data));
        }
        process_->Cancel();
        process_.reset();
        finished_ = Failure("TIMEOUT",
                            "The background engine did not finish in time and was stopped; run fewer ticks (60 per "
                            "second of game time).",
                            0u);
        return *finished_;
    }
    const uint32_t exitCode = process_->ExitCode();
    process_.reset();
    finished_ = Failure("ENGINE_FAILED",
                        "The background engine ended without a result (exit code " + std::to_string(exitCode) +
                            "); see log and problems.",
                        exitCode);
    return *finished_;
}

} // namespace HIKARI::EDITOR
