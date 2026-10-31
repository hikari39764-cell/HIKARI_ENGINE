#include "Runtime/Diagnostics/HIKARI_PerformanceCapture.h"

#include <algorithm>
#include <array>
#include <ctime>
#include <exception>
#include <memory>
#include <Windows.h>
#include <Psapi.h>
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Diagnostics/HIKARI_BenchmarkRun.h"
#include "Diagnostics/HIKARI_MetricRegistry.h"
#include "Diagnostics/HIKARI_TraceRecorder.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneJointPaletteUpload.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Assets/Environment/HIKARI_EnvironmentAsset.h"

namespace HIKARI::RUNTIME::PERFORMANCE_CAPTURE {
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;

namespace {
// 測定条件として、環境の参照・解決済みのプロファイル・その時点の主光源を残す。
Json DescribeEnvironment(const DocumentSceneBase& document) {
    const SceneEnvironment& environment = document.GetSceneEnvironment();
    const EnvironmentDirectionalLight& light = environment.frame.Primary();
    return {{"binding", ENVIRONMENT::WriteSceneEnvironmentBinding(document.GetSceneDocument().environment)},
            {"profile", ENVIRONMENT::WriteEnvironmentProfileSections(environment)},
            {"primaryLight",
             {{"toLight", {light.toLight.x, light.toLight.y, light.toLight.z}},
              {"illuminance", light.illuminance},
              {"moon", environment.frame.moonIsPrimary},
              {"localHours", environment.frame.localHours}}}};
}

// 実行中のバイナリの識別。比較の「同じ構成か」に使う（リンクのたびに変わる）。
Json DescribeBuild() {
    Json build = Json::object();
    const HMODULE module = GetModuleHandleW(nullptr);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos != nullptr && dos->e_magic == IMAGE_DOS_SIGNATURE) {
        const auto* nt =
            reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const BYTE*>(module) + dos->e_lfanew);
        if (nt->Signature == IMAGE_NT_SIGNATURE) {
            build["binaryTimestamp"] = nt->FileHeader.TimeDateStamp;
            build["imageSize"] = nt->OptionalHeader.SizeOfImage;
        }
    }
    return build;
}

// 登録表の全指標を、最後に見えた状態のまま書き出す。値のない指標は値を書かず、状態で示す。
Json DescribeMetrics(const std::vector<METRIC::MetricReading>& metrics) {
    Json result = Json::object();
    for (const METRIC::MetricReading& reading : metrics) {
        const METRIC::MetricDescriptor& descriptor = *reading.descriptor;
        Json entry = {{"unit", descriptor.unit},   {"category", METRIC::ToString(descriptor.category)},
                      {"owner", descriptor.owner}, {"module", descriptor.module},
                      {"help", descriptor.help},   {"status", METRIC::ToString(reading.status)}};
        if (reading.status == METRIC::MetricStatus::Fresh || reading.status == METRIC::MetricStatus::Stale) {
            entry["value"] = reading.value;
            entry["sourceFrame"] = reading.sourceFrame;
        } else if (reading.status == METRIC::MetricStatus::Unsupported) {
            entry["note"] = reading.note;
        }
        result[descriptor.id] = std::move(entry);
    }
    return result;
}

// 1 回の捕獲。
struct Session {
    explicit Session(Request value) : request(std::move(value)), run(request.warmupFrames, request.sampleFrames) {}
    Request request;
    DIAGNOSTICS::BenchmarkRun run;
    Clock::time_point previousPresent{};
    bool hasApplicationPresent = false;
    bool finished = false;
    bool writeSucceeded = false;
    bool configurationStable = true;
    bool nativeFullQuality = false;
    bool temporalValid = false;
    bool traceStarted = false;
    uint32_t drainFrames = 0;
    uint64_t previousRenderFailures = 0, rejectedRenderSamples = 0;
    uint64_t peakObjects = 0, peakAuthorVisibleObjects = 0, peakMassInstances = 0;
    uint64_t peakWorkingSet = 0, peakPrivateBytes = 0, peakVram = 0;
    uint64_t jointPaletteMatrixPayloadBytes = 0, jointPaletteSnapshotUploads = 0;
    uint64_t jointPaletteSnapshotReuses = 0, jointPaletteEmptyBindingUploads = 0;
    uint64_t minimumVramBudget = UINT64_MAX;
    uint64_t lastFrameId = 0;
    uint32_t processMemorySamples = 0, videoMemorySamples = 0;
    RENDER3D::RenderQualitySettings initialQuality{};
    std::array<uint32_t, 6> initialDimensions{};
    uint64_t initialDeviceGeneration = 0;
    bool initialDebugLayer = false, initialGpuValidation = false;
    bool initialGpuFrameProfiler = false, initialGpuPipelineStats = false;
    std::string initialSceneId;
    uint64_t initialSceneRevision = 0;
    Json context = Json::object();
    Json startup = Json::object();
    Json report = Json::object();
    std::string message;

    void SampleMemory(const std::vector<METRIC::MetricReading>& metrics) {
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                    sizeof(memory))) {
            ++processMemorySamples;
            peakWorkingSet = (std::max)(peakWorkingSet, static_cast<uint64_t>(memory.WorkingSetSize));
            peakPrivateBytes = (std::max)(peakPrivateBytes, static_cast<uint64_t>(memory.PrivateUsage));
        }
        const METRIC::MetricReading* usage = METRIC::FindReading(metrics, METRIC::kVramUsage);
        const METRIC::MetricReading* budget = METRIC::FindReading(metrics, METRIC::kVramBudget);
        if (usage != nullptr && budget != nullptr && usage->status == METRIC::MetricStatus::Fresh &&
            budget->status == METRIC::MetricStatus::Fresh) {
            ++videoMemorySamples;
            peakVram = (std::max)(peakVram, static_cast<uint64_t>(usage->value));
            minimumVramBudget = (std::min)(minimumVramBudget, static_cast<uint64_t>(budget->value));
        }
    }

    void CaptureContext(const IScene* scene) {
        const auto& graphics = GFX::GetCurrentContext();
        const auto temporal = RENDER3D::TEMPORAL::GetLatestTemporalFrameState(RENDER3D::kPrimaryRenderViewId);
        const auto& quality = RENDER3D::GetRenderQualitySettings();
        const auto& debug = GFX::GetGfxDebugConfig();
        const auto* document = dynamic_cast<const DocumentSceneBase*>(scene);
        const std::array<uint32_t, 6> dimensions{temporal.renderWidth,
                                                 temporal.renderHeight,
                                                 temporal.outputWidth,
                                                 temporal.outputHeight,
                                                 static_cast<uint32_t>(graphics.backBufferWidth),
                                                 static_cast<uint32_t>(graphics.backBufferHeight)};
        if (run.SampleCount() > 1) {
            configurationStable &= initialQuality == quality && initialDimensions == dimensions &&
                                   initialDeviceGeneration == graphics.deviceGeneration &&
                                   initialDebugLayer == debug.enableDebugLayer &&
                                   initialGpuValidation == debug.enableGpuBasedValidation &&
                                   initialGpuFrameProfiler == debug.enableGpuFrameProfiler &&
                                   initialGpuPipelineStats == debug.enableGpuPipelineStats;
            temporalValid &= temporal.completed && !temporal.aborted;
            configurationStable &= (scene ? scene->GetSceneId() : "") == initialSceneId &&
                                   (!document || document->GetSceneDocumentRevision() == initialSceneRevision);
            if (run.CaptureComplete() && document)
                context["environmentAtEnd"] = DescribeEnvironment(*document);
            return;
        }
        initialQuality = quality;
        initialDimensions = dimensions;
        initialDeviceGeneration = graphics.deviceGeneration;
        initialDebugLayer = debug.enableDebugLayer;
        initialGpuValidation = debug.enableGpuBasedValidation;
        initialGpuFrameProfiler = debug.enableGpuFrameProfiler;
        initialGpuPipelineStats = debug.enableGpuPipelineStats;
        initialSceneId = scene ? scene->GetSceneId() : "";
        initialSceneRevision = document ? document->GetSceneDocumentRevision() : 0;
        temporalValid = temporal.completed && !temporal.aborted;
        nativeFullQuality = temporal.renderWidth == temporal.outputWidth &&
                            temporal.renderHeight == temporal.outputHeight && !debug.enableDebugLayer &&
                            !debug.enableGpuBasedValidation &&
                            quality.frameGenerationMode == RENDER3D::RenderFrameGenerationMode::Off &&
                            quality.forwardCostMode == RENDER3D::ForwardShadingCostMode::Full;
        context = {
            {"project", TEXT::PathToUtf8(GetProjectContext().GetRoot())},
            {"renderQuality", RENDER3D::SerializeRenderQualitySettings(quality)},
            {"dimensions",
             {{"render", {dimensions[0], dimensions[1]}},
              {"output", {dimensions[2], dimensions[3]}},
              {"backBuffer", {dimensions[4], dimensions[5]}}}},
            {"debugLayer", initialDebugLayer},
            {"gpuValidation", initialGpuValidation},
            {"profiling",
             {{"enableGpuFrameProfiler", initialGpuFrameProfiler},
              {"enableGpuPipelineStats", initialGpuPipelineStats}}},
            {"deviceGeneration", initialDeviceGeneration},
            {"sceneId", initialSceneId},
            {"sceneDocumentRevision", initialSceneRevision},
            {"editorUiEnabled", SERVICES::IsEditorUIEnabled()},
            {"cameraAtStart",
             {{"position", {temporal.camera.cameraPos.x, temporal.camera.cameraPos.y, temporal.camera.cameraPos.z}},
              {"fovYRadians", temporal.camera.fovYRad},
              {"nearZ", temporal.camera.nearZ},
              {"farZ", temporal.camera.farZ}}},
            {"cacheState", "uncontrolled"},
            {"workload", "active project; fixture/camera controlled by caller"},
            {"build", DescribeBuild()}};
        if (document) {
            const Camera3D& camera = document->GetCamera();
            context["cameraAtStart"]["target"] = {camera.GetTarget().x, camera.GetTarget().y, camera.GetTarget().z};
            context["cameraAtStart"]["up"] = {camera.GetUp().x, camera.GetUp().y, camera.GetUp().z};
            context["environmentAtStart"] = DescribeEnvironment(*document);
            if (run.CaptureComplete())
                context["environmentAtEnd"] = context["environmentAtStart"];
        }
#if defined(HIKARI_SHIPPING)
        context["configuration"] = "Shipping";
#elif defined(NDEBUG)
        context["configuration"] = "EditorRelease";
#else
        context["configuration"] = "Debug";
#endif
        if (const auto adapter = GFX::VIDEO_MEMORY::GetAdapterInfo()) {
            context["adapter"] = {{"name", adapter->name},
                                  {"vendorId", adapter->vendorId},
                                  {"deviceId", adapter->deviceId},
                                  {"dedicatedVideoMemoryBytes", adapter->dedicatedVideoMemoryBytes}};
        }
    }

    // 採取が始まる直前（熱身の最後のフレームの後）から、時間線の記録を始める。
    void StartTraceWhenSampling() {
        if (request.tracePath.empty() || traceStarted || !run.WarmupComplete())
            return;
        // 1 フレームの区間の数は多くても数百。溢れた分は捨てて数える（記録中に確保しない）。
        TRACING::Start(std::clamp<size_t>(static_cast<size_t>(request.sampleFrames) * 400u, 100000u, 1500000u));
        traceStarted = true;
    }

    void OnPresentedFrame(uint64_t frameId, bool applicationFrame, const IScene* scene) {
        if (finished)
            return;
        const auto now = Clock::now();
        const uint64_t renderFailures = RENDER3D::PIPELINE::GetPrimaryFrameFailureCount();
        const bool renderRejected = renderFailures != previousRenderFailures;
        previousRenderFailures = renderFailures;
        const bool measureStartup = request.startupStarted != Clock::time_point{};
        const double startupMs = std::chrono::duration<double, std::milli>(now - request.startupStarted).count();
        if (measureStartup && startup.empty())
            startup["firstPresentMs"] = startupMs;
        if (!applicationFrame)
            return;
        if (!hasApplicationPresent) {
            if (measureStartup)
                startup["firstApplicationPresentMs"] = startupMs;
            hasApplicationPresent = true;
            previousPresent = now;
            StartTraceWhenSampling();
            return;
        }
        const double intervalMs = std::chrono::duration<double, std::milli>(now - previousPresent).count();
        previousPresent = now;
        lastFrameId = frameId;
        const std::vector<METRIC::MetricReading> metrics = METRIC::Snapshot(frameId);
        if (run.RecordFrame(frameId, intervalMs, metrics)) {
            if (renderRejected)
                ++rejectedRenderSamples;
            CaptureContext(scene);
            const auto& jointPaletteUpload =
                RENDER3D::GPUDRIVEN::GetGpuSceneJointPaletteUploadStats(GFX::GetCurrentContext());
            jointPaletteMatrixPayloadBytes += jointPaletteUpload.matrixPayloadBytes;
            jointPaletteSnapshotUploads += jointPaletteUpload.snapshotUploadCount;
            jointPaletteSnapshotReuses += jointPaletteUpload.snapshotReuseCount;
            jointPaletteEmptyBindingUploads += jointPaletteUpload.emptyBindingUploadCount;
            const auto& sceneStats = RenderSubmissionSystem::GetSceneRenderCacheStats();
            peakObjects = (std::max)(peakObjects, static_cast<uint64_t>(sceneStats.renderObjectCount));
            peakAuthorVisibleObjects =
                (std::max)(peakAuthorVisibleObjects, static_cast<uint64_t>(sceneStats.authorVisibleObjectCount));
            peakMassInstances =
                (std::max)(peakMassInstances,
                           static_cast<uint64_t>(RenderSubmissionSystem::GetDebugStats().massRenderInstanceCount));
            if (run.SampleCount() == 1 || run.SampleCount() % 60 == 0 || run.CaptureComplete())
                SampleMemory(metrics);
        }
        run.RecordGpu(metrics);
        StartTraceWhenSampling();
        // GPU の末尾回読は追加フレームで回収し、計測用の同期待機は行わない。
        if (run.CaptureComplete() && (run.GpuCoverageComplete() || ++drainFrames >= 8))
            Finish();
    }

    // 時間線を書き、書いた結果（件数・溢れ・GPU の時計が合っているか）を返す。書けなければ message に理由を残す。
    Json WriteTrace() {
        TRACING::Stop();
        const uint64_t firstFrame = report.value("firstFrameId", uint64_t{1});
        const uint64_t lastFrame = report.contains("lastFrameId") ? report.value("lastFrameId", uint64_t{0}) : 0u;
        TRACING::TraceStats stats{};
        std::string error;
        const bool written = TRACING::Write(request.tracePath, firstFrame, lastFrame, stats, error);
        TRACING::Clear();
        if (!written) {
            message = "The trace was not written: " + error;
            return {{"path", TEXT::PathToUtf8(request.tracePath)}, {"written", false}, {"error", error}};
        }
        return {{"path", TEXT::PathToUtf8(request.tracePath)},
                {"written", true},
                {"events", stats.events},
                {"droppedEvents", stats.dropped},
                {"gpuClockAligned", stats.gpuClockAligned}};
    }

    void Finish() {
        if (finished)
            return;
        finished = true;
        try {
            report = run.BuildReport();
            report["context"] = context;
            report["startup"] = startup;
            report["origin"] = request.commandLine ? "commandLine" : "operation";
            report["capturedAt"] = LocalTimestamp(false);
            report["configurationStable"] = configurationStable;
            report["rejectedRenderSamples"] = rejectedRenderSamples;
            report["nativeFullQualityComparable"] = run.CaptureComplete() && configurationStable && temporalValid &&
                                                    nativeFullQuality && rejectedRenderSamples == 0 &&
                                                    report["invalidIntervals"] == 0;
            report["scenePeaks"] = {{"objects", peakObjects},
                                    {"authorVisibleObjects", peakAuthorVisibleObjects},
                                    {"massRenderInstances", peakMassInstances}};
            report["jointPaletteUploads"] = {{"sampleFrames", run.SampleCount()},
                                             {"matrixPayloadBytes", jointPaletteMatrixPayloadBytes},
                                             {"snapshotUploads", jointPaletteSnapshotUploads},
                                             {"snapshotReuses", jointPaletteSnapshotReuses},
                                             {"emptyBindingUploads", jointPaletteEmptyBindingUploads}};
            report["memory"] = {
                {"samplingIntervalFrames", 60},
                {"processSamples", processMemorySamples},
                {"videoMemorySamples", videoMemorySamples},
                {"peakWorkingSetBytes", processMemorySamples ? Json(peakWorkingSet) : Json(nullptr)},
                {"peakPrivateBytes", processMemorySamples ? Json(peakPrivateBytes) : Json(nullptr)},
                {"peakLocalVideoUsageBytes", videoMemorySamples ? Json(peakVram) : Json(nullptr)},
                {"minimumLocalVideoBudgetBytes", videoMemorySamples ? Json(minimumVramBudget) : Json(nullptr)}};
            const std::vector<METRIC::MetricReading> metrics = METRIC::Snapshot(lastFrameId);
            const METRIC::MetricReading* gpuFrame = METRIC::FindReading(metrics, METRIC::kFrameGpuMs);
            report["gpuUnavailableReasonAtFinish"] =
                gpuFrame != nullptr && gpuFrame->status == METRIC::MetricStatus::Unsupported ? gpuFrame->note
                                                                                             : std::string();
            report["metricsAtFinish"] = DescribeMetrics(metrics);
            const auto& fg = RENDER3D::UPSCALING::GetStreamlineFrameGenerationStats();
            report["frameGenerationAtFinish"] = {{"status", RENDER3D::UPSCALING::ToString(fg.status)},
                                                 {"requested", fg.requested},
                                                 {"actuallyPresentedFrames", fg.presentedFrames}};
            report["trace"] = traceStarted ? WriteTrace() : Json(nullptr);
            std::error_code directoryError;
            if (request.output.has_parent_path())
                std::filesystem::create_directories(request.output.parent_path(), directoryError);
            std::string writeMessage;
            writeSucceeded = SERIALIZATION::JSON::WriteJsonFile(request.output, report, &writeMessage);
            if (writeSucceeded) {
                CORE::LogInfo("[Benchmark] report saved: " + TEXT::PathToUtf8(request.output));
            } else {
                message = writeMessage;
                CORE::LogError("[Benchmark] report failed: " + writeMessage);
            }
        } catch (const std::exception& error) {
            message = error.what();
            CORE::LogError(std::string("[Benchmark] report failed: ") + error.what());
        }
        if (traceStarted)
            TRACING::Clear();
    }
};

std::unique_ptr<Session> gSession;
// 最後に終わった捕獲。
Status gLast;
bool gExitRequested = false;
bool gCommandLine = false;
int gExitCode = 0;

Status StatusOf(const Session& session) {
    Status status;
    status.state = CaptureState::Running;
    status.warmupFrames = session.request.warmupFrames;
    status.warmedFrames = session.run.WarmedFrameCount();
    status.sampleFrames = session.request.sampleFrames;
    status.sampledFrames = session.run.SampleCount();
    status.output = session.request.output;
    status.tracePath = session.request.tracePath;
    return status;
}

// 終わった捕獲を最後の結果に移し、次の捕獲を始められる状態にする。
void Conclude() {
    Session& session = *gSession;
    gLast = StatusOf(session);
    gLast.state = session.writeSucceeded ? CaptureState::Finished : CaptureState::Failed;
    gLast.message = session.message;
    gLast.report = std::move(session.report);
    gCommandLine = session.request.commandLine;
    gExitRequested = session.request.exitWhenFinished;
    gExitCode = session.writeSucceeded && session.run.CaptureComplete() && session.configurationStable &&
                        session.rejectedRenderSamples == 0
                    ? 0
                    : 2;
    gSession.reset();
}
} // namespace

std::string LocalTimestamp(bool forFileName) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char text[32]{};
    std::strftime(text, sizeof(text), forFileName ? "%Y%m%d-%H%M%S" : "%Y-%m-%dT%H:%M:%S", &local);
    return text;
}

bool Start(Request request, std::string& error) {
    if (gSession) {
        error = "A performance capture is already running.";
        return false;
    }
    if (request.output.empty()) {
        error = "A performance capture needs an output path.";
        return false;
    }
    gLast = {};
    gSession = std::make_unique<Session>(std::move(request));
    return true;
}

void StartFromCommandLine(Clock::time_point startupStarted) {
    const auto output = ReadRuntimeCommandLinePath(L"--benchmark-output");
    if (!output)
        return;
    Request request;
    request.output = *output;
    if (const auto trace = ReadRuntimeCommandLinePath(L"--benchmark-trace"))
        request.tracePath = *trace;
    request.warmupFrames = ReadRuntimeCommandLineUint32(L"--benchmark-warmup-frames").value_or(request.warmupFrames);
    request.sampleFrames = ReadRuntimeCommandLineUint32(L"--benchmark-sample-frames").value_or(request.sampleFrames);
    request.exitWhenFinished = HasRuntimeCommandLineFlag(L"--benchmark-exit");
    request.commandLine = true;
    request.startupStarted = startupStarted;
    std::string error;
    if (!Start(std::move(request), error)) {
        CORE::LogError("[Benchmark] " + error);
        return;
    }
    CORE::LogInfo("[Benchmark] capture requested: " + TEXT::PathToUtf8(*output));
}

void OnPresentedFrame(uint64_t frameId, bool applicationFrame, const IScene* scene) {
    if (!gSession)
        return;
    gSession->OnPresentedFrame(frameId, applicationFrame, scene);
    if (gSession->finished)
        Conclude();
}

Status GetStatus() { return gSession ? StatusOf(*gSession) : gLast; }

bool ShouldExitHost() noexcept { return gExitRequested; }

int ExitCode() noexcept { return gCommandLine ? gExitCode : 0; }

void Shutdown() {
    if (!gSession)
        return;
    gSession->Finish();
    Conclude();
}

} // namespace HIKARI::RUNTIME::PERFORMANCE_CAPTURE
