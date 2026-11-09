#include "Editor/Performance/HIKARI_PerformanceAuditPanel.h"

#include "Core/HIKARI_TimeService.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Material/HIKARI_GpuMaterialRegistry.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Render3D/ScreenSpace/HIKARI_SsaoRenderer.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"
#include "Render3D/Upscaling/HIKARI_StreamlineReflex.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#include "imgui.h"

#include "Editor/Performance/HIKARI_PerformanceAuditInternal.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>
#include <locale>
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"

namespace HIKARI::EDITOR::PERFORMANCE_AUDIT {

const char* ReadyText(bool value) { return value ? "Ready" : "Missing"; }

double SafeDivide(uint64_t numerator, uint64_t denominator) {
    return denominator != 0u ? static_cast<double>(numerator) / static_cast<double>(denominator) : 0.0;
}

std::vector<PipelineStatsRow> PipelineStatsRows(const std::vector<METRIC::MetricReading>& metrics) {
    struct Item {
        std::string_view name;
        const METRIC::MetricReading* PipelineStatsRow::* field;
    };
    static constexpr Item kItems[] = {
        {"calls", &PipelineStatsRow::calls},
        {"asInvocations", &PipelineStatsRow::asInvocations},
        {"msInvocations", &PipelineStatsRow::msInvocations},
        {"msPrimitives", &PipelineStatsRow::msPrimitives},
        {"clipPrimitives", &PipelineStatsRow::clipPrimitives},
        {"psInvocations", &PipelineStatsRow::psInvocations},
    };
    constexpr std::string_view kMarker = ".pipeline.";
    constexpr std::string_view kDomain = "gpu.";

    std::vector<PipelineStatsRow> rows;
    for (const METRIC::MetricReading& reading : metrics) {
        const std::string_view id = reading.descriptor->id;
        const size_t marker = id.rfind(kMarker);
        if (!id.starts_with(kDomain) || marker == std::string_view::npos || marker <= kDomain.size()) {
            continue;
        }
        const std::string_view name = id.substr(kDomain.size(), marker - kDomain.size());
        const std::string_view item = id.substr(marker + kMarker.size());
        auto row = std::find_if(rows.begin(), rows.end(), [name](const PipelineStatsRow& r) { return r.name == name; });
        if (row == rows.end()) {
            row = rows.insert(rows.end(), PipelineStatsRow{name});
        }
        for (const Item& entry : kItems) {
            if (entry.name == item) {
                (*row).*entry.field = &reading;
            }
        }
    }
    return rows;
}

void AppendShadowCacheReason(std::string& text, uint32_t flags, uint32_t bit, const char* label) {
    if ((flags & bit) == 0u) {
        return;
    }
    if (!text.empty()) {
        text += "+";
    }
    text += label;
}

std::string ShadowCacheMissReasonText(uint32_t flags) {
    if (flags == SHADOW::ShadowCacheMissReasonNone) {
        return "none";
    }
    std::string text{};
    AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonNoStaticWork, "no static work");
    AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonStaticDirty, "static dirty");
    AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonMatrix, "matrix");
    AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonSource, "source");
    AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonLayout, "layout");
    AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonInstanceCount, "instance count");
    AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonResolution, "resolution");
    AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonResource, "resource");
    AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonState, "state");
    AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonInvalid, "invalid");
    return text.empty() ? "mixed" : text;
}

ImVec4 StatusColor(bool ok) { return ok ? ImVec4(0.28f, 0.82f, 0.45f, 1.0f) : ImVec4(0.95f, 0.34f, 0.32f, 1.0f); }

void TextStatus(const char* label, bool ok) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::TextColored(StatusColor(ok), "%s", ok ? "Ready" : "Missing");
}

void MetricRowText(const char* label, const char* value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted(value);
}

RuntimePerformanceSnapshot BuildSnapshot() {
    RuntimePerformanceSnapshot out{};
    const FrameContext& frame = TIME::GetFrameContext();
    const GFX::Context& graphics = GFX::GetCurrentContext();
    out.metrics = METRIC::Snapshot(frame.frameIndex);
    out.fixedStepsThisFrame = frame.fixedStepsThisFrame;
    out.droppedFixedTimeSeconds = frame.droppedFixedTime;
    out.sceneId = RuntimeSceneContext::GetCurrentSceneId();
    out.debugConfig = GFX::GetGfxDebugConfig();
    out.renderQuality = RENDER3D::GetRenderQualitySettings();
    out.backBufferWidth = graphics.backBufferWidth;
    out.backBufferHeight = graphics.backBufferHeight;
    out.submission = RenderSubmissionSystem::GetDebugStats();
    out.scene = RenderSubmissionSystem::GetSceneRenderCacheStats();
    out.gpuRegistry = RenderSubmissionSystem::GetGpuSceneRegistryStats();
    out.jointPaletteUpload = RENDER3D::GPUDRIVEN::GetGpuSceneJointPaletteUploadStats(graphics);
    out.gpuMaterials = MESHRENDERER::GetGpuMaterialRegistryStats();
    out.mesh = MESHRENDERER::GetDebugStats();
    out.shadow = SHADOW::GetDebugStats();
    out.localShadow = SHADOW::GetLocalLightShadowStats();
    out.dynamicGi = RENDER3D::PIPELINE::GetDynamicDiffuseGiStats();
    out.rayScene = RENDER3D::PIPELINE::GetRuntimeRayTracingStats();
    out.clusterResources = RENDER3D::GetClusterGeometryResourceSystemStats();
    out.descriptorPool = RENDER3D::GetRenderResourceDescriptorPoolStats();
    out.ssao = RENDER3D::SCREENSPACE::GetSsaoDebugState();
    out.depthVisibility = RENDER3D::SCREENSPACE::GetDepthVisibilityDebugState();
    out.volumetric = RENDER3D::VOLUMETRIC::GetVolumetricLightingStats();
    out.clouds = RENDER3D::CLOUDS::GetVolumetricCloudStageStats();
    out.temporalFrame = RENDER3D::TEMPORAL::GetLatestTemporalFrameState(RENDER3D::kPrimaryRenderViewId);
    out.temporalResources = RENDER3D::TEMPORAL::GetLatestTemporalResourceStats(RENDER3D::kPrimaryRenderViewId);
    out.streamline = RENDER3D::UPSCALING::GetStreamlineDebugStats();
    out.streamlineReflex = RENDER3D::UPSCALING::GetStreamlineReflexStats();
    out.streamlineFrameGeneration = RENDER3D::UPSCALING::GetStreamlineFrameGenerationStats();
    out.presentation = POST::PostSystem::GetPresentationFrameResources();
    return out;
}

namespace {
const char* StatusName(const METRIC::MetricReading* reading) {
    return reading != nullptr ? METRIC::ToString(reading->status) : METRIC::ToString(METRIC::MetricStatus::NoData);
}

// レポート用の値。値のない状態は 0 ではなく状態名で書く。scale で単位を割る。
std::string ReportValue(const METRIC::MetricReading* reading, int precision = 3, double scale = 1.0) {
    if (reading == nullptr || reading->status == METRIC::MetricStatus::NoData ||
        reading->status == METRIC::MetricStatus::Unsupported) {
        return StatusName(reading);
    }
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(precision) << reading->value / scale;
    if (reading->status == METRIC::MetricStatus::Stale) {
        text << "(stale)";
    }
    return text.str();
}

// CPU / GPU の計時を 1 行にする。値のある指標（Fresh か Stale）だけを書く。
void AppendTimingLine(std::ostream& out, const char* domain, std::string_view name, const METRIC::MetricReading* ms,
                      const METRIC::MetricReading* calls, const METRIC::MetricReading* selfMs) {
    if (ms == nullptr || (ms->status != METRIC::MetricStatus::Fresh && ms->status != METRIC::MetricStatus::Stale)) {
        return;
    }
    out << domain << ' ' << name << " ms: " << ReportValue(ms);
    if (calls != nullptr) {
        out << " calls=" << ReportValue(calls, 0);
    }
    if (selfMs != nullptr) {
        out << " self=" << ReportValue(selfMs);
    }
    out << " avg/p95/p99/max=" << ms->window.average << '/' << ms->window.p95 << '/' << ms->window.p99 << '/'
        << ms->window.maximum << " samples=" << ms->window.sampleCount << '\n';
}
} // namespace

std::string BuildPerformanceReport(const RuntimePerformanceSnapshot& s) {
    static constexpr TEXT::EnumName<RENDER3D::UPSCALING::StreamlineReflexMode> reflexModes[] = {
        {RENDER3D::UPSCALING::StreamlineReflexMode::Off, "Off"},
        {RENDER3D::UPSCALING::StreamlineReflexMode::LowLatency, "LowLatency"},
        {RENDER3D::UPSCALING::StreamlineReflexMode::LowLatencyWithBoost, "Boost"},
    };
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(3);
    out << "HIKARI performance snapshot\n";
    constexpr double kMiB = 1048576.0;
    if (const auto adapter = GFX::VIDEO_MEMORY::GetAdapterInfo()) {
        out << "Adapter: " << adapter->name
            << "\nDedicated VRAM MiB: " << static_cast<double>(adapter->dedicatedVideoMemoryBytes) / kMiB << '\n';
    } else {
        out << "Adapter: unavailable\n";
    }
    out << "Local VRAM usage / budget MiB: " << ReportValue(METRIC::FindReading(s.metrics, METRIC::kVramUsage), 3, kMiB)
        << " / " << ReportValue(METRIC::FindReading(s.metrics, METRIC::kVramBudget), 3, kMiB) << '\n';
    out << "Scene render: " << s.temporalFrame.renderWidth << 'x' << s.temporalFrame.renderHeight
        << "\nScene output: " << s.temporalFrame.outputWidth << 'x' << s.temporalFrame.outputHeight
        << "\nBack buffer: " << s.backBufferWidth << 'x' << s.backBufferHeight
        << "\nAA: " << RENDER3D::RenderAntiAliasingModeLabel(s.renderQuality.antiAliasingMode)
        << "\nFPS raw: " << ReportValue(METRIC::FindReading(s.metrics, METRIC::kFrameFps), 1) << '\n';
    out << "Scene: " << (s.sceneId.empty() ? "unavailable" : s.sceneId) << '\n';
    out << "Camera valid/source/position: " << s.temporalFrame.camera.valid << '/'
        << s.temporalFrame.cameraSourceObjectId << "/(" << s.temporalFrame.camera.cameraPos.x << ','
        << s.temporalFrame.camera.cameraPos.y << ',' << s.temporalFrame.camera.cameraPos.z << ")\n"
        << "Camera FOV Y radians / near / far / aspect: " << s.temporalFrame.camera.fovYRad << '/'
        << s.temporalFrame.camera.nearZ << '/' << s.temporalFrame.camera.farZ << '/' << s.temporalFrame.camera.aspect
        << '\n';
    out << "Camera view matrix (rows):\n";
    for (int row = 0; row < 4; ++row) {
        out << "  " << s.temporalFrame.camera.view.m[0][row] << ' ' << s.temporalFrame.camera.view.m[1][row] << ' '
            << s.temporalFrame.camera.view.m[2][row] << ' ' << s.temporalFrame.camera.view.m[3][row] << '\n';
    }
    out << "Render quality settings: " << RENDER3D::SerializeRenderQualitySettings(s.renderQuality).dump() << '\n';
    const auto& fg = s.streamlineFrameGeneration;
    out << "Frame Gen requested/loaded/supported/host: " << fg.requested << '/' << fg.featureLoaded << '/'
        << fg.supported << '/' << fg.hostAllowed << '\n';
    out << "Frame Gen runtime/reason: " << RENDER3D::UPSCALING::ToString(fg.status) << " / "
        << (fg.statusReason.empty() ? "none" : fg.statusReason) << '\n';
    out << "Frame Gen inputs/options/tags/state: " << fg.inputsReady << '/' << fg.optionsConfigured << '/'
        << fg.tagsSubmitted << '/' << fg.stateValid << '\n';
    out << "Frame Gen generated requested/max; actually presented: " << fg.generatedFrames << '/'
        << fg.maxGeneratedFrames << "; " << fg.presentedFrames << '\n';
    out << "Frame Gen status flags/failures: " << fg.statusFlags << '/' << fg.failureCount << '\n';
    out << "Reflex plugin / PCL plugin / support / PCL support: " << s.streamlineReflex.reflexPluginPresent << '/'
        << s.streamlineReflex.pclPluginPresent << '/' << s.streamlineReflex.reflexSupported << '/'
        << s.streamlineReflex.pclSupported << '\n';
    out << "Reflex mode / options / sleep / marker mask / failures: "
        << TEXT::EnumToName(reflexModes, s.streamlineReflex.mode, "Unknown") << '/'
        << s.streamlineReflex.optionsConfigured << '/' << s.streamlineReflex.sleepCalled << "/0x" << std::hex
        << static_cast<unsigned int>(s.streamlineReflex.markerMask) << std::dec << '/'
        << s.streamlineReflex.failureCount << '\n';
    out << "Debug layer configured: " << s.debugConfig.enableDebugLayer
        << " GPU validation configured: " << s.debugConfig.enableGpuBasedValidation << '\n';
    out << "GPU timing / pipeline statistics configured: " << s.debugConfig.enableGpuFrameProfiler << '/'
        << s.debugConfig.enableGpuPipelineStats << '\n';
    const METRIC::MetricReading* cpuFrame = METRIC::FindReading(s.metrics, METRIC::kFrameCpuMs);
    out << "CPU frame: " << (cpuFrame != nullptr ? cpuFrame->sourceFrame : 0u) << " status=" << StatusName(cpuFrame)
        << '\n';
    AppendTimingLine(out, "CPU", CPU_PROFILE::ToString(CPU_PROFILE::Pass::Frame), cpuFrame, nullptr, nullptr);
    for (const METRIC::TimingRow& row : METRIC::TimingRows(s.metrics, "cpu")) {
        AppendTimingLine(out, "CPU", row.name, row.ms, row.calls, row.selfMs);
    }
    const METRIC::MetricReading* gpuFrame = METRIC::FindReading(s.metrics, METRIC::kFrameGpuMs);
    out << "GPU frame: " << (gpuFrame != nullptr ? gpuFrame->sourceFrame : 0u) << " status=" << StatusName(gpuFrame)
        << '\n';
    if (gpuFrame != nullptr && gpuFrame->status == METRIC::MetricStatus::Unsupported) {
        out << "GPU reason: " << gpuFrame->note << '\n';
    }
    AppendTimingLine(out, "GPU", "Command Frame", gpuFrame, nullptr, nullptr);
    for (const METRIC::TimingRow& row : METRIC::TimingRows(s.metrics, "gpu")) {
        AppendTimingLine(out, "GPU", row.name, row.ms, row.calls, row.selfMs);
    }
    out << "GPU timings are pass samples, not additive frame time; nested passes overlap.\n";
    out << "GPU ms sums every execution of the pass in the frame (calls = executions).\n";
    out << "Rolling window: last 300 frames in which the metric was measured; quantiles refresh every 15 samples.\n";
    const METRIC::MetricReading* pipelineExecutions = METRIC::FindReading(s.metrics, METRIC::kGpuPipelineExecutions);
    out << "Pipeline stats frame: " << (pipelineExecutions != nullptr ? pipelineExecutions->sourceFrame : 0u)
        << " status=" << StatusName(pipelineExecutions) << '\n';
    if (pipelineExecutions != nullptr && pipelineExecutions->status == METRIC::MetricStatus::Unsupported) {
        out << "Pipeline stats reason: " << pipelineExecutions->note << '\n';
    }
    for (const PipelineStatsRow& row : PipelineStatsRows(s.metrics)) {
        if (IsFresh(row.calls)) {
            out << "Pipeline " << row.name << " PS/MS invocations; MS triangles: " << ReportValue(row.psInvocations, 0)
                << '/' << ReportValue(row.msInvocations, 0) << "; " << ReportValue(row.msPrimitives, 0) << '\n';
        }
    }
    out << "Scene HZB builds/Aux reused: " << s.depthVisibility.sceneDepthPyramidBuildCount << '/'
        << s.depthVisibility.auxiliaryDepthReused << '\n';
    const auto& visibility = s.mesh.meshletBackend.visibility;
    out << "AS Forward HZB bound/width/height/mips: " << visibility.forwardHzbBound << '/' << visibility.width << '/'
        << visibility.height << '/' << visibility.mipCount << '\n';
    for (size_t phase = 0; phase < visibility.samples.size(); ++phase) {
        const auto& sample = visibility.samples[phase];
        out << "AS " << RENDER3D::MESHLET::kMeshletVisibilityPhaseNames[phase]
            << " sampled(1/16) valid=" << sample.valid << " retireFence=" << sample.retireFence;
        if (sample.valid) {
            const auto& c = sample.counters;
            out << " candidates/submitted/invalid=" << c.candidates << '/' << c.submitted << '/' << c.invalid
                << " frustumCulled=" << c.frustumCulled << " coneTested/culled=" << c.coneTested << '/' << c.coneCulled
                << " hzbTested/culled=" << c.hzbTested << '/' << c.hzbCulled << " unbounded=" << c.unbounded;
        }
        out << '\n';
    }
    out << "Shadow source full/patch/reuse; updated records: " << s.shadow.shadowSourceFullSyncCount << '/'
        << s.shadow.shadowSourcePatchCount << '/' << s.shadow.shadowSourceReuseCount << "; "
        << s.shadow.shadowSourceUpdatedInstanceCount << '\n';
    const std::string currentShadowCacheReason = ShadowCacheMissReasonText(s.shadow.shadowCacheMissReasonFlags);
    const std::string lastShadowCacheReason = ShadowCacheMissReasonText(s.shadow.shadowCacheLastMissReasonFlags);
    out << "Directional shadow enabled/resolution/cascades/PCF/radius: " << s.shadow.enabled << '/'
        << s.shadow.resolution << '/' << s.shadow.cascadeCount << '/' << s.shadow.pcfEnabled << '/'
        << s.shadow.pcfRadius << '\n';
    out << "Directional shadow ortho/near/far/depth bias/normal bias/strength: " << s.shadow.orthoSize << '/'
        << s.shadow.nearPlane << '/' << s.shadow.farPlane << '/' << s.shadow.depthBias << '/' << s.shadow.normalBias
        << '/' << s.shadow.strength << '\n';
    out << "Shadow depth cache valid/hit/current reason/last reason/last miss logical frame: "
        << s.shadow.shadowCacheValid << '/' << s.shadow.shadowCacheHit << '/' << currentShadowCacheReason << '/'
        << lastShadowCacheReason << '/' << s.shadow.shadowCacheLastMissFrameIndex << '\n';
    out << "Shadow depth cache hits/misses/copy/update: " << s.shadow.shadowCacheHitCount << '/'
        << s.shadow.shadowCacheMissCount << '/' << s.shadow.shadowStaticCacheCopyCount << '/'
        << s.shadow.shadowStaticCacheUpdateCount << '\n';
    out << "Shadow static/dynamic sources and rendered static/dynamic/fallback: "
        << s.shadow.shadowStaticSourceInstanceCount << '/' << s.shadow.shadowDynamicSourceInstanceCount << '/'
        << s.shadow.shadowStaticRendered << '/' << s.shadow.shadowDynamicRendered << '/'
        << s.shadow.shadowFallbackRendered << '\n';
    out << "Probe sampling / dynamic GI quality / status / resident / updated / primary rays / bytes: "
        << RENDER3D::LightProbeVolumeSamplingModeLabel(s.renderQuality.lightProbeVolumeSampling) << '/'
        << RENDER3D::DynamicGiQualityLabel(s.renderQuality.dynamicGiQuality) << '/'
        << RENDER3D::DYNAMICGI::ToString(s.dynamicGi.status) << '/' << s.dynamicGi.probes << '/'
        << s.dynamicGi.updatedProbes << '/' << s.dynamicGi.primaryRays << '/' << s.dynamicGi.probeBytes << '\n';
    out << "Scene objects total/author-visible/hidden: " << s.scene.renderObjectCount << '/'
        << s.scene.authorVisibleObjectCount << '/' << s.scene.hiddenObjectCount
        << "\nSurfaces total/dirty: " << s.scene.surfaceInstanceCount << '/' << s.scene.dirtySurfaceInstanceCount
        << '\n';
    out << "GPU scene CPU sync: " << RENDER3D::GPUDRIVEN::ToString(s.gpuRegistry.syncMode)
        << "; records rebuilt this frame: " << s.gpuRegistry.frameRecordBuildCount << '\n';
    out << "GPU scene records reused/hidden: " << s.gpuRegistry.frameRecordReuseCount << '/'
        << s.gpuRegistry.hiddenSourceRecordCount << '\n';
    if (s.gpuRegistry.syncMode == RENDER3D::GPUDRIVEN::GpuSceneSyncMode::RebuildPatchRejected) {
        const auto& rejection = s.gpuRegistry.patchRejection;
        out << "GPU scene patch rejection: " << rejection.reason << "; object=" << rejection.objectId
            << "; surface=" << rejection.surfaceIndex << "; node=" << rejection.nodeIndex
            << "; model=" << rejection.modelPath << '\n';
    }
    out << "GPU scene upload full/patch/reuse: " << s.mesh.gpuSceneUpload.uploadedFullScene << '/'
        << s.mesh.gpuSceneUpload.patchedDirtyRanges << '/' << s.mesh.gpuSceneUpload.reusedResidentFrame << '\n';
    out << "GPU scene upload committed instances/bytes: " << s.mesh.gpuSceneUpload.bufferStats.committedInstanceCount
        << '/' << s.mesh.gpuSceneUpload.bufferStats.committedBytes << '\n';
    out << "Joint palette matrix payload bytes / uploads / reuse: " << s.jointPaletteUpload.matrixPayloadBytes << '/'
        << s.jointPaletteUpload.snapshotUploadCount << '/' << s.jointPaletteUpload.snapshotReuseCount << '\n';
    // 前のフレームの upload 量（0 でない所有者だけ）。
    out << "Upload bytes last frame:";
    for (const METRIC::MetricReading& reading : s.metrics)
        if (reading.descriptor->id.starts_with("upload.") && reading.value > 0.0)
            out << ' ' << reading.descriptor->id << '=' << reading.value;
    out << '\n';
    return out.str();
}

} // namespace HIKARI::EDITOR::PERFORMANCE_AUDIT
