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

namespace HIKARI::EDITOR::PERFORMANCE_AUDIT {

#if defined(HIKARI_WITH_EDITOR)
bool IsClusterSubpass(GFX::GPU_PROFILE::Pass pass) {
  return pass == GFX::GPU_PROFILE::Pass::ClusterCull ||
         pass == GFX::GPU_PROFILE::Pass::TraditionalDrawGeometryAux ||
         pass == GFX::GPU_PROFILE::Pass::TraditionalDrawForward ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawGeometryAux ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawDepthPrepass ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawForward ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawDepthAware ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawTransparent;
}

bool IsShadowSubpass(GFX::GPU_PROFILE::Pass pass) {
  return pass == GFX::GPU_PROFILE::Pass::TraditionalDrawShadow ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadow ||
         pass == GFX::GPU_PROFILE::Pass::TraditionalDrawShadowStatic ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadowStatic ||
         pass == GFX::GPU_PROFILE::Pass::TraditionalDrawShadowDynamic ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadowDynamic ||
         pass == GFX::GPU_PROFILE::Pass::TraditionalDrawShadowFallback ||
         pass == GFX::GPU_PROFILE::Pass::MeshletDrawShadowFallback;
}

bool IsNestedGpuSubpass(GFX::GPU_PROFILE::Pass pass) {
  return IsClusterSubpass(pass) || IsShadowSubpass(pass);
}

const char *TimingScopeText(GFX::GPU_PROFILE::Pass pass) {
  if (IsShadowSubpass(pass)) {
    return "Shadow Subpass";
  }
  return IsClusterSubpass(pass) ? "Cluster Subpass" : "Parent Pass";
}

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
  out.submission = RenderSubmissionSystem::GetDebugStats();
  out.scene = RenderSubmissionSystem::GetSceneRenderCacheStats();
  out.gpuRegistry = RenderSubmissionSystem::GetGpuSceneRegistryStats();
  out.gpuMaterials = MESHRENDERER::GetGpuMaterialRegistryStats();
  out.mesh = MESHRENDERER::GetDebugStats();
  out.shadow = SHADOW::GetDebugStats();
  out.clusterResources = RENDER3D::GetClusterGeometryResourceSystemStats();
  out.descriptorPool = RENDER3D::GetRenderResourceDescriptorPoolStats();
  out.ssao = RENDER3D::SCREENSPACE::GetSsaoDebugState();
  out.depthVisibility = RENDER3D::SCREENSPACE::GetDepthVisibilityDebugState();
  out.volumetric = RENDER3D::VOLUMETRIC::GetVolumetricLightingStats();
  out.temporalFrame = RENDER3D::TEMPORAL::GetCurrentTemporalFrameState();
  out.temporalResources = RENDER3D::TEMPORAL::GetTemporalResourceStats();
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
    if (timing.valid && IsNestedGpuSubpass(pass) == nestedSubpass) {
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
#endif

} // namespace HIKARI::EDITOR::PERFORMANCE_AUDIT
