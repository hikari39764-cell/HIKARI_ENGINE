#include "Runtime/Diagnostics/HIKARI_RuntimeBenchmark.h"

#include <algorithm>
#include <array>
#include <exception>
#include <Windows.h>
#include <Psapi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Diagnostics/HIKARI_BenchmarkRun.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
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

namespace HIKARI::RUNTIME {
using Clock = std::chrono::steady_clock;
using Json = nlohmann::json;
using Timing = DIAGNOSTICS::BenchmarkTiming;

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
} // namespace

struct RuntimeBenchmark::State {
    State(std::filesystem::path path, uint32_t warmup, uint32_t samples, Clock::time_point start)
        : output(std::move(path)), run(warmup, samples), startupStarted(start) {}
    std::filesystem::path output;
    DIAGNOSTICS::BenchmarkRun run;
    Clock::time_point startupStarted;
    Clock::time_point previousPresent{};
    bool hasApplicationPresent = false;
    bool finished = false;
    bool writeSucceeded = false;
    bool exitWhenFinished = false;
    bool configurationStable = true;
    bool nativeFullQuality = false;
    bool temporalValid = false;
    uint32_t drainFrames = 0;
    uint64_t previousRenderFailures = 0, rejectedRenderSamples = 0;
    uint64_t peakObjects = 0, peakAuthorVisibleObjects = 0, peakMassInstances = 0;
    uint64_t peakWorkingSet = 0, peakPrivateBytes = 0, peakVram = 0;
    uint64_t jointPaletteMatrixPayloadBytes = 0, jointPaletteSnapshotUploads = 0;
    uint64_t jointPaletteSnapshotReuses = 0, jointPaletteEmptyBindingUploads = 0;
    uint64_t minimumVramBudget = UINT64_MAX;
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
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;

    void SampleMemory() {
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        if (K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
                                    sizeof(memory))) {
            ++processMemorySamples;
            peakWorkingSet = (std::max)(peakWorkingSet, static_cast<uint64_t>(memory.WorkingSetSize));
            peakPrivateBytes = (std::max)(peakPrivateBytes, static_cast<uint64_t>(memory.PrivateUsage));
        }
        DXGI_QUERY_VIDEO_MEMORY_INFO video{};
        if (adapter && SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &video))) {
            ++videoMemorySamples;
            peakVram = (std::max)(peakVram, video.CurrentUsage);
            minimumVramBudget = (std::min)(minimumVramBudget, video.Budget);
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
            {"workload", "active project; fixture/camera controlled by caller"}};
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
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        if (graphics.device && SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) &&
            SUCCEEDED(factory->EnumAdapterByLuid(graphics.device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)))) {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                char name[512]{};
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
                context["adapter"] = {{"name", name},
                                      {"vendorId", desc.VendorId},
                                      {"deviceId", desc.DeviceId},
                                      {"dedicatedVideoMemoryBytes", desc.DedicatedVideoMemory}};
            }
        }
    }
};

RuntimeBenchmark::RuntimeBenchmark(std::unique_ptr<State> state) : state_(std::move(state)) {}
RuntimeBenchmark::~RuntimeBenchmark() = default;

std::unique_ptr<RuntimeBenchmark> RuntimeBenchmark::FromCommandLine(Clock::time_point startupStarted) {
    const auto output = ReadRuntimeCommandLinePath(L"--benchmark-output");
    if (!output)
        return {};
    auto state = std::make_unique<State>(
        *output, ReadRuntimeCommandLineUint32(L"--benchmark-warmup-frames").value_or(180),
        ReadRuntimeCommandLineUint32(L"--benchmark-sample-frames").value_or(600), startupStarted);
    state->exitWhenFinished = HasRuntimeCommandLineFlag(L"--benchmark-exit");
    CORE::LogInfo("[Benchmark] capture requested: " + TEXT::PathToUtf8(*output));
    return std::unique_ptr<RuntimeBenchmark>(new RuntimeBenchmark(std::move(state)));
}

void RuntimeBenchmark::OnPresentedFrame(uint64_t frameId, bool applicationFrame, const IScene* scene) {
    auto& s = *state_;
    if (s.finished)
        return;
    const auto now = Clock::now();
    const uint64_t renderFailures = RENDER3D::PIPELINE::GetPrimaryFrameFailureCount();
    const bool renderRejected = renderFailures != s.previousRenderFailures;
    s.previousRenderFailures = renderFailures;
    const double startupMs = std::chrono::duration<double, std::milli>(now - s.startupStarted).count();
    if (s.startup.empty())
        s.startup["firstPresentMs"] = startupMs;
    if (!applicationFrame)
        return;
    if (!s.hasApplicationPresent) {
        s.startup["firstApplicationPresentMs"] = startupMs;
        s.hasApplicationPresent = true;
        s.previousPresent = now;
        return;
    }
    const double intervalMs = std::chrono::duration<double, std::milli>(now - s.previousPresent).count();
    s.previousPresent = now;
    std::array<Timing, static_cast<size_t>(CPU_PROFILE::Pass::Count) + CPU_PROFILE::kMaximumDynamicPasses> cpu{};
    size_t count = 0;
    const auto& cpuSnapshot = CPU_PROFILE::GetLatestSnapshot();
    if (cpuSnapshot.valid && cpuSnapshot.frameIndex == frameId) {
        for (const auto& pass : cpuSnapshot.passes)
            if (pass.valid)
                cpu[count++] = {pass.name, pass.cpuMs};
        for (uint32_t i = 0;
             i < (std::min)(cpuSnapshot.dynamicPassCount, static_cast<uint32_t>(CPU_PROFILE::kMaximumDynamicPasses));
             ++i) {
            const auto& pass = cpuSnapshot.dynamicPasses[i];
            if (pass.valid)
                cpu[count++] = {pass.name, pass.cpuMs};
        }
    }
    if (s.run.RecordFrame(frameId, intervalMs, {cpu.data(), count})) {
        if (renderRejected)
            ++s.rejectedRenderSamples;
        s.CaptureContext(scene);
        const auto& jointPaletteUpload =
            RENDER3D::GPUDRIVEN::GetGpuSceneJointPaletteUploadStats(GFX::GetCurrentContext());
        s.jointPaletteMatrixPayloadBytes += jointPaletteUpload.matrixPayloadBytes;
        s.jointPaletteSnapshotUploads += jointPaletteUpload.snapshotUploadCount;
        s.jointPaletteSnapshotReuses += jointPaletteUpload.snapshotReuseCount;
        s.jointPaletteEmptyBindingUploads += jointPaletteUpload.emptyBindingUploadCount;
        const auto& sceneStats = RenderSubmissionSystem::GetSceneRenderCacheStats();
        s.peakObjects = (std::max)(s.peakObjects, static_cast<uint64_t>(sceneStats.renderObjectCount));
        s.peakAuthorVisibleObjects =
            (std::max)(s.peakAuthorVisibleObjects, static_cast<uint64_t>(sceneStats.authorVisibleObjectCount));
        s.peakMassInstances =
            (std::max)(s.peakMassInstances,
                       static_cast<uint64_t>(RenderSubmissionSystem::GetDebugStats().massRenderInstanceCount));
        if (s.run.SampleCount() == 1 || s.run.SampleCount() % 60 == 0 || s.run.CaptureComplete())
            s.SampleMemory();
    }
    const auto& gpuSnapshot = GFX::GPU_PROFILE::GetLatestSnapshot();
    if (gpuSnapshot.gpuTimingAvailable) {
        std::array<Timing, static_cast<size_t>(GFX::GPU_PROFILE::Pass::Count)> gpu{};
        count = 0;
        for (const auto& pass : gpuSnapshot.passes)
            if (pass.valid)
                gpu[count++] = {pass.name, pass.gpuMs};
        if (count > 0)
            s.run.RecordGpu(gpuSnapshot.frameIndex, {gpu.data(), count});
    }
    // GPU の末尾回読は追加フレームで回収し、計測用の同期待機は行わない。
    if (s.run.CaptureComplete() && (s.run.GpuCoverageComplete() || ++s.drainFrames >= 8))
        Finish();
}

void RuntimeBenchmark::Finish() {
    auto& s = *state_;
    if (s.finished)
        return;
    s.finished = true;
    try {
        auto report = s.run.BuildReport();
        report["context"] = s.context;
        report["startup"] = s.startup;
        report["configurationStable"] = s.configurationStable;
        report["rejectedRenderSamples"] = s.rejectedRenderSamples;
        report["nativeFullQualityComparable"] = s.run.CaptureComplete() && s.configurationStable && s.temporalValid &&
                                                s.nativeFullQuality && s.rejectedRenderSamples == 0 &&
                                                report["invalidIntervals"] == 0;
        report["scenePeaks"] = {{"objects", s.peakObjects},
                                {"authorVisibleObjects", s.peakAuthorVisibleObjects},
                                {"massRenderInstances", s.peakMassInstances}};
        report["jointPaletteUploads"] = {{"sampleFrames", s.run.SampleCount()},
                                         {"matrixPayloadBytes", s.jointPaletteMatrixPayloadBytes},
                                         {"snapshotUploads", s.jointPaletteSnapshotUploads},
                                         {"snapshotReuses", s.jointPaletteSnapshotReuses},
                                         {"emptyBindingUploads", s.jointPaletteEmptyBindingUploads}};
        report["memory"] = {
            {"samplingIntervalFrames", 60},
            {"processSamples", s.processMemorySamples},
            {"videoMemorySamples", s.videoMemorySamples},
            {"peakWorkingSetBytes", s.processMemorySamples ? Json(s.peakWorkingSet) : Json(nullptr)},
            {"peakPrivateBytes", s.processMemorySamples ? Json(s.peakPrivateBytes) : Json(nullptr)},
            {"peakLocalVideoUsageBytes", s.videoMemorySamples ? Json(s.peakVram) : Json(nullptr)},
            {"minimumLocalVideoBudgetBytes", s.videoMemorySamples ? Json(s.minimumVramBudget) : Json(nullptr)}};
        const auto& gpu = GFX::GPU_PROFILE::GetLatestSnapshot();
        report["gpuUnavailableReasonAtFinish"] = gpu.unavailableReason ? gpu.unavailableReason : "";
        const auto& fg = RENDER3D::UPSCALING::GetStreamlineFrameGenerationStats();
        report["frameGenerationAtFinish"] = {{"status", RENDER3D::UPSCALING::ToString(fg.status)},
                                             {"requested", fg.requested},
                                             {"actuallyPresentedFrames", fg.presentedFrames}};
        std::string message;
        s.writeSucceeded = SERIALIZATION::JSON::WriteJsonFile(s.output, report, &message);
        if (s.writeSucceeded)
            CORE::LogInfo("[Benchmark] report saved: " + TEXT::PathToUtf8(s.output));
        else
            CORE::LogError("[Benchmark] report failed: " + message);
    } catch (const std::exception& error) {
        CORE::LogError(std::string("[Benchmark] report failed: ") + error.what());
    }
}

bool RuntimeBenchmark::ShouldExit() const noexcept { return state_->finished && state_->exitWhenFinished; }
int RuntimeBenchmark::ExitCode() const noexcept {
    return state_->writeSucceeded && state_->run.CaptureComplete() && state_->configurationStable &&
                   state_->rejectedRenderSamples == 0
               ? 0
               : 2;
}

} // namespace HIKARI::RUNTIME
