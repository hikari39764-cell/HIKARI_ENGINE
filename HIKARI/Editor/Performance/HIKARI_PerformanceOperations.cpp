#include "Editor/Performance/HIKARI_PerformanceOperations.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Diagnostics/HIKARI_BenchmarkRun.h"
#include "Diagnostics/HIKARI_PerformanceAnalysis.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Runtime/Diagnostics/HIKARI_PerformanceCapture.h"

namespace HIKARI::EDITOR {
namespace {
using Json = nlohmann::json;
namespace CAPTURE = RUNTIME::PERFORMANCE_CAPTURE;

constexpr const char* kCapturePrefix = "capture-";
constexpr const char* kTraceSuffix = ".trace.json";

std::filesystem::path PerformanceDirectory() { return GetProjectContext().GetLibraryRoot() / "Performance"; }

// 保存済みの捕獲（時間線は除く）を古い順に。名前に時刻を入れてあるので、名前順が時間順。
std::vector<std::filesystem::path> ListCaptures() {
    std::vector<std::filesystem::path> captures;
    std::error_code error;
    for (std::filesystem::directory_iterator it(PerformanceDirectory(), error), end; !error && it != end;
         it.increment(error)) {
        const std::string name = it->path().filename().string();
        if (it->is_regular_file(error) && name.rfind(kCapturePrefix, 0) == 0 && name.ends_with(".json") &&
            !name.ends_with(kTraceSuffix))
            captures.push_back(it->path());
    }
    std::sort(captures.begin(), captures.end());
    return captures;
}

// "latest" / "previous"、または Library/Performance の中のファイル（名前だけ、.json は省いてよい）。
bool ResolveCapture(const std::string& text, std::filesystem::path& outPath, std::string& error) {
    const std::vector<std::filesystem::path> captures = ListCaptures();
    if (text.empty() || text == "latest" || text == "previous") {
        const size_t back = text == "previous" ? 2u : 1u;
        if (captures.size() < back) {
            error = captures.empty() ? "There is no saved capture yet. Run perf.capture first."
                                     : "There is only one saved capture.";
            return false;
        }
        outPath = captures[captures.size() - back];
        return true;
    }
    std::filesystem::path candidate = TEXT::PathFromUtf8(text);
    if (candidate.extension() != ".json")
        candidate += ".json";
    if (candidate.is_relative())
        candidate = PerformanceDirectory() / candidate;
    std::error_code ignored;
    const std::filesystem::path directory = std::filesystem::weakly_canonical(PerformanceDirectory(), ignored);
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(candidate, ignored);
    if (resolved.parent_path() != directory) {
        error = "A capture must be a file inside Library/Performance: " + text;
        return false;
    }
    if (!std::filesystem::is_regular_file(resolved, ignored)) {
        error = "No such capture: " + text;
        return false;
    }
    outPath = resolved;
    return true;
}

bool LoadCapture(const std::filesystem::path& path, Json& outCapture, std::string& error) {
    if (!SERIALIZATION::JSON::ReadJsonFile(path, outCapture, &error))
        return false;
    error = DIAGNOSTICS::CaptureSchemaError(outCapture);
    return error.empty();
}

Json DescribeCaptureFile(const std::filesystem::path& path, const Json& capture) {
    return {{"path", TEXT::PathToUtf8(path)},
            {"capturedAt", capture.value("capturedAt", std::string{})},
            {"origin", capture.value("origin", std::string{})}};
}

constexpr TEXT::EnumName<CAPTURE::CaptureState> kStateNames[] = {
    {CAPTURE::CaptureState::Idle, "idle"},
    {CAPTURE::CaptureState::Running, "running"},
    {CAPTURE::CaptureState::Finished, "finished"},
    {CAPTURE::CaptureState::Failed, "failed"},
};

EditorOperationResult StartCapture(const Json& parameters, const std::function<DocumentSceneBase*()>& sceneProvider) {
    if (!GetProjectContext().IsOpen())
        return OperationFailure("NO_PROJECT", "Open a project first.");
    if (sceneProvider() == nullptr)
        return OperationFailure("NO_SCENE", "No scene is open.");
    const uint32_t maximum = DIAGNOSTICS::kMaximumCaptureFrames;
    CAPTURE::Request request;
    const int limit = static_cast<int>(maximum);
    request.warmupFrames =
        static_cast<uint32_t>(std::clamp(parameters.value("warmup", static_cast<int>(request.warmupFrames)), 0, limit));
    request.sampleFrames = static_cast<uint32_t>(
        std::clamp(parameters.value("samples", static_cast<int>(request.sampleFrames)), 1, limit));
    const std::string stem = std::string(kCapturePrefix) + CAPTURE::LocalTimestamp(true);
    request.output = PerformanceDirectory() / (stem + ".json");
    if (parameters.value("trace", false))
        request.tracePath = PerformanceDirectory() / (stem + kTraceSuffix);
    std::string error;
    if (!CAPTURE::Start(request, error))
        return OperationFailure("CAPTURE_BUSY", error);
    Json data = {{"state", "running"},
                 {"warmupFrames", request.warmupFrames},
                 {"sampleFrames", request.sampleFrames},
                 {"path", TEXT::PathToUtf8(request.output)},
                 {"tracePath", request.tracePath.empty() ? Json(nullptr) : Json(TEXT::PathToUtf8(request.tracePath))},
                 {"pending", true},
                 {"poll", {{"operation", "perf.capture.status"}, {"parameters", Json::object()}}}};
    return OperationOk("Performance capture started: " + std::to_string(request.warmupFrames) + " warm-up and " +
                           std::to_string(request.sampleFrames) +
                           " sample frames of the current editor world. Keep the editor view as it is until it "
                           "finishes, then poll perf.capture.status.",
                       std::move(data));
}

EditorOperationResult CaptureStatus() {
    const CAPTURE::Status status = CAPTURE::GetStatus();
    Json data = {{"state", TEXT::EnumToName(kStateNames, status.state, "idle")},
                 {"warmupFrames", status.warmupFrames},
                 {"warmedFrames", status.warmedFrames},
                 {"sampleFrames", status.sampleFrames},
                 {"sampledFrames", status.sampledFrames}};
    if (status.state == CAPTURE::CaptureState::Idle)
        return OperationOk("No performance capture has run in this session.", std::move(data));
    data["path"] = TEXT::PathToUtf8(status.output);
    data["tracePath"] = status.tracePath.empty() ? Json(nullptr) : Json(TEXT::PathToUtf8(status.tracePath));
    if (status.state == CAPTURE::CaptureState::Running) {
        data["pending"] = true;
        data["poll"] = {{"operation", "perf.capture.status"}, {"parameters", Json::object()}};
        return OperationOk("Capturing: " + std::to_string(status.warmedFrames) + "/" +
                               std::to_string(status.warmupFrames) + " warm-up, " +
                               std::to_string(status.sampledFrames) + "/" + std::to_string(status.sampleFrames) +
                               " sample frames.",
                           std::move(data));
    }
    if (!status.message.empty())
        data["message"] = status.message;
    if (status.state == CAPTURE::CaptureState::Failed) {
        EditorOperationResult failure =
            OperationFailure("CAPTURE_FAILED", "The capture could not be saved: " + status.message);
        failure.data = std::move(data);
        return failure;
    }
    data["trace"] = status.report.value("trace", Json(nullptr));
    data["highlights"] = DIAGNOSTICS::BuildCaptureHighlights(status.report);
    data["next"] = "perf.findings explains the capture; perf.compare <a> <b> compares two captures.";
    return OperationOk("Capture saved to " + TEXT::PathToUtf8(status.output), std::move(data));
}

EditorOperationResult CompareOperation(const Json& parameters) {
    if (!GetProjectContext().IsOpen())
        return OperationFailure("NO_PROJECT", "Open a project first.");
    std::filesystem::path paths[2];
    Json captures[2];
    const char* keys[2] = {"a", "b"};
    for (int index = 0; index < 2; ++index) {
        std::string error;
        if (!ResolveCapture(parameters.value(keys[index], std::string{}), paths[index], error) ||
            !LoadCapture(paths[index], captures[index], error))
            return OperationFailure("INVALID_CAPTURE", std::string(keys[index]) + ": " + error);
    }
    const size_t limit = static_cast<size_t>(std::clamp(parameters.value("limit", 40), 1, 400));
    Json result = DIAGNOSTICS::CompareCaptures(captures[0], captures[1], limit);
    result["a"] = DescribeCaptureFile(paths[0], captures[0]);
    result["b"] = DescribeCaptureFile(paths[1], captures[1]);
    const Json& summary = result["summary"];
    return OperationOk("Compared the captures: " + std::to_string(summary.value("regressions", 0)) + " regressions, " +
                           std::to_string(summary.value("improvements", 0)) + " improvements.",
                       std::move(result));
}

EditorOperationResult FindingsOperation(const Json& parameters) {
    if (!GetProjectContext().IsOpen())
        return OperationFailure("NO_PROJECT", "Open a project first.");
    std::filesystem::path path;
    Json capture;
    std::string error;
    if (!ResolveCapture(parameters.value("capture", std::string{}), path, error) || !LoadCapture(path, capture, error))
        return OperationFailure("INVALID_CAPTURE", error);
    Json result = DIAGNOSTICS::BuildFindings(capture);
    result["capture"] = DescribeCaptureFile(path, capture);
    const Json& summary = result["summary"];
    return OperationOk("Findings: " + std::to_string(summary.value("critical", 0)) + " critical, " +
                           std::to_string(summary.value("warning", 0)) + " warning, " +
                           std::to_string(summary.value("info", 0)) + " info.",
                       std::move(result));
}
} // namespace

void RegisterPerformanceOperations(EditorOperationRegistry& registry,
                                   std::function<DocumentSceneBase*()> sceneProvider) {
    using P = EditorOperationParameterType;
    const Json frameRange = {{"minimum", 0}, {"maximum", DIAGNOSTICS::kMaximumCaptureFrames}};
    const CAPTURE::Request defaults;

    // capture.preview と同じ宣言：セッションの操作で、文書の確認も権限の承認も要らない（保存先は Library の中）。
    EditorOperationDefinition capture{
        "perf.capture",
        "Capture performance of the current editor world: skip warmup frames, then record samples frames from the "
        "metric registry (frame time, CPU scopes, GPU passes, VRAM, per-module cost). The summary is saved to the "
        "project's Library/Performance. With trace=true a chrome://tracing timeline of the sampled frames is saved "
        "too. Poll perf.capture.status; the editor view must stay as it is while it runs.",
        "general",
        {{"warmup", P::Int, false,
          "Frames to skip before sampling; default " + std::to_string(defaults.warmupFrames) + ".", frameRange},
         {"samples",
          P::Int,
          false,
          "Frames to record; default " + std::to_string(defaults.sampleFrames) + ".",
          {{"minimum", 1}, {"maximum", DIAGNOSTICS::kMaximumCaptureFrames}}},
         {"trace", P::Bool, false,
          "Also write a chrome://tracing timeline (CPU and GPU tracks) of the sampled frames."}},
        [sceneProvider](const Json& parameters) { return StartCapture(parameters, sceneProvider); }};
    capture.searchTerms = {"performance", "profile", "benchmark", "fps", "frame time", "性能", "性能分析", "捕获"};
    registry.Register(std::move(capture), EditorOperationKind::Session, false);

    EditorOperationDefinition status{
        "perf.capture.status",
        "Report the running performance capture, or the highlights of the last finished one.",
        "general",
        {},
        [](const Json&) { return CaptureStatus(); }};
    status.permission = EditorOperationPermission::ReadEditorState;
    registry.Register(std::move(status), EditorOperationKind::Query, false);

    EditorOperationDefinition compare{
        "perf.compare",
        "Compare two saved performance captures metric by metric: difference, percentage and a regression or "
        "improvement verdict (time-like metrics must pass both an absolute and a relative threshold). Also says "
        "whether the captures share build, scene, quality and GPU.",
        "general",
        {{"a", P::String, true, "The baseline capture: a file name in Library/Performance, 'latest' or 'previous'."},
         {"b", P::String, true, "The capture to judge: a file name in Library/Performance, 'latest' or 'previous'."},
         {"limit", P::Int, false, "Most changed metrics to list; default 40.", {{"minimum", 1}, {"maximum", 400}}}},
        [](const Json& parameters) { return CompareOperation(parameters); }};
    compare.permission = EditorOperationPermission::ReadEditorState;
    compare.searchTerms = {"performance", "regression", "diff", "性能", "对比"};
    registry.Register(std::move(compare), EditorOperationKind::Query, false);

    EditorOperationDefinition findings{
        "perf.findings",
        "Explain a saved performance capture with rules: whether the CPU or the GPU limits the frame, the most "
        "expensive passes and their modules, passes with unusually many calls, how close video memory is to its "
        "budget, and which metrics are unsupported or stale and why. Every finding names the metric ids it rests on.",
        "general",
        {{"capture", P::String, false, "A file name in Library/Performance, 'latest' (default) or 'previous'."}},
        [](const Json& parameters) { return FindingsOperation(parameters); }};
    findings.permission = EditorOperationPermission::ReadEditorState;
    findings.searchTerms = {"performance", "bottleneck", "analysis", "性能", "瓶颈"};
    registry.Register(std::move(findings), EditorOperationKind::Query, false);
}

} // namespace HIKARI::EDITOR
