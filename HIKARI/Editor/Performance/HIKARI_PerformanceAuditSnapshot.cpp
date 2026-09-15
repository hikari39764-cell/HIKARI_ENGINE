#include "Editor/Performance/HIKARI_PerformanceAuditPanel.h"

#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_GpuPipelineStatsProfiler.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Material/HIKARI_GpuMaterialRegistry.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Render3D/ScreenSpace/HIKARI_SsaoRenderer.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"
#include "Render3D/Upscaling/HIKARI_StreamlineReflex.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

#include "Editor/Performance/HIKARI_PerformanceAuditInternal.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>
#include <locale>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"


namespace HIKARI::EDITOR::PERFORMANCE_AUDIT {

#if defined(HIKARI_WITH_EDITOR)
const char *ReadyText(bool value) { return value ? "Ready" : "Missing"; }

double SafeDivide(uint64_t numerator, uint64_t denominator) {
  return denominator != 0u
             ? static_cast<double>(numerator) / static_cast<double>(denominator)
             : 0.0;
}

bool IsMeshletPipelineStatsPass(GFX::GPU_PROFILE::Pass pass) {
  return pass == GFX::GPU_PROFILE::Pass::MeshletDrawGeometryAux ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawDepthPrepass ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawForward ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawDepthAware ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawTransparent ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadow ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadowStatic ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadowDynamic ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadowFallback;
}

void AppendShadowCacheReason(std::string &text, uint32_t flags, uint32_t bit,
                             const char *label) {

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
  AppendShadowCacheReason(
      text, flags, SHADOW::ShadowCacheMissReasonNoStaticWork, "no static work");
  AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonStaticDirty,
                          "static dirty");
  AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonMatrix,
                          "matrix");
  AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonSource,
                          "source");
  AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonLayout,
                          "layout");
  AppendShadowCacheReason(text, flags,
                          SHADOW::ShadowCacheMissReasonInstanceCount,
                          "instance count");
  AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonResolution,
                          "resolution");
  AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonResource,
                          "resource");
  AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonState,
                          "state");
  AppendShadowCacheReason(text, flags, SHADOW::ShadowCacheMissReasonInvalid,
                          "invalid");
  return text.empty() ? "mixed" : text;
}

ImVec4 StatusColor(bool ok) {
  return ok ? ImVec4(0.28f, 0.82f, 0.45f, 1.0f)
            : ImVec4(0.95f, 0.34f, 0.32f, 1.0f);
}

void TextStatus(const char *label, bool ok) {
  ImGui::TextUnformatted(label);
  ImGui::SameLine();
  ImGui::TextColored(StatusColor(ok), "%s", ok ? "Ready" : "Missing");
}

void MetricRowText(const char *label, const char *value) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::TextUnformatted(label);
  ImGui::TableSetColumnIndex(1);
  ImGui::TextUnformatted(value);
}

RuntimePerformanceSnapshot BuildSnapshot() {
  RuntimePerformanceSnapshot out{};
  const FrameContext &frame = TIME::GetFrameContext();
  out.fpsRaw = frame.rawDt > 0.0f ? 1.0f / frame.rawDt : 0.0f;
  out.fixedStepsThisFrame = frame.fixedStepsThisFrame;
  out.droppedFixedTimeSeconds = frame.droppedFixedTime;
  out.submission = RenderSubmissionSystem::GetDebugStats();
  out.scene = RenderSubmissionSystem::GetSceneRenderCacheStats();
  out.gpuRegistry = RenderSubmissionSystem::GetGpuSceneRegistryStats();
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
  out.clouds = RENDER3D::CLOUDS::GetStylizedCloudStageStats();
  out.temporalFrame = RENDER3D::TEMPORAL::GetLatestTemporalFrameState(
      RENDER3D::kPrimaryRenderViewId);
  out.temporalResources =
      RENDER3D::TEMPORAL::GetLatestTemporalResourceStats(
          RENDER3D::kPrimaryRenderViewId);
  out.streamline = RENDER3D::UPSCALING::GetStreamlineDebugStats();
  out.streamlineReflex = RENDER3D::UPSCALING::GetStreamlineReflexStats();
  out.streamlineFrameGeneration =
      RENDER3D::UPSCALING::GetStreamlineFrameGenerationStats();
  out.presentation = POST::PostSystem::GetPresentationFrameResources();
  out.cpu = CPU_PROFILE::GetLatestSnapshot();
  out.gpu = GFX::GPU_PROFILE::GetLatestSnapshot();
  out.pipelineStats = GFX::GPU_PIPELINE_STATS::GetLatestSnapshot();
  return out;
}

double SumGpuMsByScope(const GFX::GPU_PROFILE::FrameSnapshot &profile,
                       bool nestedSubpass) {

  double total = 0.0;
  for (size_t i = 0; i < profile.passes.size(); ++i) {
    const GFX::GPU_PROFILE::Pass pass = static_cast<GFX::GPU_PROFILE::Pass>(i);
    const GFX::GPU_PROFILE::PassTiming &timing = profile.passes[i];
    if (pass != GFX::GPU_PROFILE::Pass::CommandFrame &&
        timing.valid && (timing.parentPass != GFX::GPU_PROFILE::Pass::Count &&
            timing.parentPass != GFX::GPU_PROFILE::Pass::CommandFrame) == nestedSubpass) {
      total += timing.gpuMs;
    }
  }
  return total;
}

uint32_t CountValidGpuPasses(const GFX::GPU_PROFILE::FrameSnapshot &profile) {
  uint32_t count = 0;
  for (const GFX::GPU_PROFILE::PassTiming &timing : profile.passes) {
    if (timing.valid) {
      ++count;
    }
  }
  return count;
}

double GetCpuFrameMs(const CPU_PROFILE::FrameSnapshot &profile) {
  const CPU_PROFILE::PassTiming &frameTiming =
      profile.passes[static_cast<size_t>(CPU_PROFILE::Pass::Frame)];
  return frameTiming.valid ? frameTiming.cpuMs : 0.0;
}

uint32_t CountValidCpuPasses(const CPU_PROFILE::FrameSnapshot &profile) {
  uint32_t count = 0;
  for (const CPU_PROFILE::PassTiming &timing : profile.passes) {
    if (timing.valid) {
      ++count;
    }
  }
  return count;
}
std::string BuildPerformanceReport(const RuntimePerformanceSnapshot& s) {
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(3);
  out << "HIKARI performance snapshot\n";
  const auto& context = GFX::GetCurrentContext();
  Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
  Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
  if (context.device && SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) &&
      SUCCEEDED(factory->EnumAdapterByLuid(context.device->GetAdapterLuid(), IID_PPV_ARGS(&adapter)))) {
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc))) {
      char name[512]{};
      WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
      out << "Adapter: " << name << "\nDedicated VRAM MiB: " << desc.DedicatedVideoMemory / 1048576.0 << '\n';
    }
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    if (SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory)))
      out << "Local VRAM usage / budget MiB: " << memory.CurrentUsage / 1048576.0 << " / " << memory.Budget / 1048576.0 << '\n';
  } else out << "Adapter: unavailable\n";
  out << "Scene render: " << s.temporalFrame.renderWidth << 'x' << s.temporalFrame.renderHeight
      << "\nScene output: " << s.temporalFrame.outputWidth << 'x' << s.temporalFrame.outputHeight
      << "\nBack buffer: " << context.backBufferWidth << 'x' << context.backBufferHeight
      << "\nAA: " << RENDER3D::RenderAntiAliasingModeLabel(RENDER3D::GetRenderQualitySettings().antiAliasingMode)
      << "\nFPS raw: " << s.fpsRaw << '\n';
  const auto& fg = s.streamlineFrameGeneration;
  out << "Frame Gen requested/loaded/supported/host: " << fg.requested << '/'
      << fg.featureLoaded << '/' << fg.supported << '/' << fg.hostAllowed << '\n';
  out << "Frame Gen runtime/reason: " << RENDER3D::UPSCALING::ToString(fg.status)
      << " / " << (fg.statusReason.empty() ? "none" : fg.statusReason) << '\n';
  out << "Frame Gen inputs/options/tags/state: " << fg.inputsReady << '/'
      << fg.optionsConfigured << '/' << fg.tagsSubmitted << '/' << fg.stateValid << '\n';
  out << "Frame Gen generated requested/max; actually presented: " << fg.generatedFrames
      << '/' << fg.maxGeneratedFrames << "; " << fg.presentedFrames << '\n';
  out << "Frame Gen status flags/failures: " << fg.statusFlags << '/' << fg.failureCount << '\n';
  const auto& debug=GFX::GetGfxDebugConfig();
  out << "Debug layer configured: " << debug.enableDebugLayer
      << " GPU validation configured: " << debug.enableGpuBasedValidation << '\n';
  out << "CPU frame: " << s.cpu.frameIndex << " valid=" << s.cpu.valid << '\n';
  const auto cpu = [&](const CPU_PROFILE::PassTiming& p) {
    if (!p.valid && p.rollingSampleCount == 0) return;
    out << "CPU " << p.name << " now/avg/p95/p99/max ms: " << p.cpuMs << '/' << p.averageMs << '/'
        << p.p95Ms << '/' << p.p99Ms << '/' << p.maximumMs << " samples=" << p.rollingSampleCount << '\n';
  };
  for (const auto& pass : s.cpu.passes) cpu(pass);
  for (uint32_t i=0; i<s.cpu.dynamicPassCount; ++i) cpu(s.cpu.dynamicPasses[i]);
  out << "GPU frame: " << s.gpu.frameIndex << " available=" << s.gpu.gpuTimingAvailable << '\n';
  if (!s.gpu.gpuTimingAvailable) out << "GPU reason: " << s.gpu.unavailableReason << '\n';
  for (size_t i=0; i<s.gpu.passes.size(); ++i) {
    const auto& pass=s.gpu.passes[i];
    if (pass.valid) out << "GPU " << pass.name << " ms: " << pass.gpuMs << " scope="
        << (i == static_cast<size_t>(GFX::GPU_PROFILE::Pass::CommandFrame) ? "command-group" :
            (pass.parentPass == GFX::GPU_PROFILE::Pass::CommandFrame || pass.parentPass == GFX::GPU_PROFILE::Pass::Count ? "parent" : "nested"))
        << " parent=" << (pass.parentPass == GFX::GPU_PROFILE::Pass::Count ? "none" : GFX::GPU_PROFILE::ToString(pass.parentPass))
        << " self=" << pass.exclusiveGpuMs << " avg/p95/p99/max=" << pass.averageMs << '/'
        << pass.p95Ms << '/' << pass.p99Ms << '/' << pass.maximumMs
        << " samples=" << pass.rollingSampleCount << '\n';
  }
  out << "GPU timings are pass samples, not additive frame time; nested passes overlap.\n";
  out << "GPU rolling window: last 300 executions per pass; quantiles refresh every 15 samples.\n";
  out << "Pipeline stats frame: " << s.pipelineStats.frameIndex
      << " available=" << s.pipelineStats.pipelineStatsAvailable << '\n';
  for (const auto& pass : s.pipelineStats.passes) {
    if (pass.valid) out << "Pipeline " << pass.name << " PS/MS invocations; MS triangles: "
        << pass.counters.PSInvocations << '/' << pass.counters.MSInvocations << "; "
        << pass.counters.MSPrimitives << '\n';
  }
  out << "Scene HZB builds/Aux reused: " << s.depthVisibility.sceneDepthPyramidBuildCount << '/'
      << s.depthVisibility.auxiliaryDepthReused << '\n';
  const auto& visibility = s.mesh.meshletBackend.visibility;
  out << "AS Forward HZB bound/width/height/mips: " << visibility.forwardHzbBound << '/'
      << visibility.width << '/' << visibility.height << '/' << visibility.mipCount << '\n';
  for (size_t phase = 0; phase < visibility.samples.size(); ++phase) {
    const auto& sample = visibility.samples[phase];
    out << "AS " << (phase == 0 ? "GeometryAux" : "Forward")
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
  out << "Scene objects total/visible/hidden: " << s.scene.renderObjectCount << '/'
      << s.scene.visibleObjectCount << '/' << s.scene.hiddenObjectCount
      << "\nSurfaces total/dirty: " << s.scene.surfaceInstanceCount << '/' << s.scene.dirtySurfaceInstanceCount << '\n';
  out << "GPU scene CPU sync: " << RENDER3D::GPUDRIVEN::ToString(s.gpuRegistry.syncMode)
      << "; records rebuilt this frame: " << s.gpuRegistry.frameRecordBuildCount << '\n';
  out << "GPU scene records reused/hidden: " << s.gpuRegistry.frameRecordReuseCount << '/'
      << s.gpuRegistry.hiddenSourceRecordCount << '\n';
  if (s.gpuRegistry.syncMode == RENDER3D::GPUDRIVEN::GpuSceneSyncMode::RebuildPatchRejected) {
    const auto& rejection = s.gpuRegistry.patchRejection;
    out << "GPU scene patch rejection: " << rejection.reason
        << "; object=" << rejection.objectId << "; surface=" << rejection.surfaceIndex
        << "; node=" << rejection.nodeIndex << "; model=" << rejection.modelPath << '\n';
  }
  out << "GPU scene upload full/patch/reuse: "
      << s.mesh.gpuSceneUpload.uploadedFullScene << '/'
      << s.mesh.gpuSceneUpload.patchedDirtyRanges << '/'
      << s.mesh.gpuSceneUpload.reusedResidentFrame << '\n';
  out << "GPU scene upload committed instances/bytes: "
      << s.mesh.gpuSceneUpload.bufferStats.committedInstanceCount << '/'
      << s.mesh.gpuSceneUpload.bufferStats.committedBytes << '\n';
  return out.str();
}
#endif

} // namespace HIKARI::EDITOR::PERFORMANCE_AUDIT
