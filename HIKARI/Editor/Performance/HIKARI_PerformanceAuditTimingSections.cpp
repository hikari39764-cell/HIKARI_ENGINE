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
void DrawFrameSummary(const RuntimePerformanceSnapshot &s) {
  const size_t mainlineSubmittedCount =
      s.mesh.meshletBackendSubmittedDispatchCount > 0
          ? s.mesh.meshletBackendSubmittedDispatchCount
          : s.mesh.clusterGpuCullGpuDrawCommandCount;
  const bool clusterMainline =
      s.mesh.clusterGpuCullSubmittedInstanceCount > 0 ||
      mainlineSubmittedCount > 0;
  const bool gpuDrivenReady =
      s.mesh.surfaceGpuSceneSrvValid && s.mesh.surfaceGpuSceneBufferReady;
  const double cpuFrameMs = GetCpuFrameMs(s.cpu);
  const double parentGpuMs = SumGpuMsByScope(s.gpu, false);
  const double nestedSubpassGpuMs = SumGpuMsByScope(s.gpu, true);

  if (ImGui::BeginTable("GpuDrivenFrameSummary", 4,
                        ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextColumn();
    ImGui::Text("FPS %.1f", s.fpsRaw);
    if (s.cpu.valid) {
      ImGui::Text("CPU frame %.3f ms", cpuFrameMs);
    } else {
      ImGui::TextDisabled("CPU timing waiting");
    }

    ImGui::TableNextColumn();
    ImGui::TextColored(StatusColor(gpuDrivenReady), "GPU scene %s",
                       ReadyText(gpuDrivenReady));
    ImGui::Text("uploaded %zu / requested %zu",
                s.mesh.surfaceGpuSceneUploadedInstanceCount,
                s.mesh.surfaceGpuSceneRequestedInstanceCount);

    ImGui::TableNextColumn();
    ImGui::TextColored(StatusColor(clusterMainline), "cluster %s",
                       clusterMainline ? "Active" : "Idle");
    ImGui::Text("dispatches %zu / draw args %zu", mainlineSubmittedCount,
                s.mesh.clusterGpuCullGpuDrawCommandCount);

    ImGui::TableNextColumn();
    ImGui::Text("GPU parent %.3f ms", parentGpuMs);
    ImGui::Text("nested sub %.3f ms", nestedSubpassGpuMs);
    ImGui::EndTable();
  }
}

void DrawCpuTimingTable(const RuntimePerformanceSnapshot &s) {
  ImGui::SeparatorText("CPU Timing");
  if (!s.cpu.valid) {
    ImGui::TextDisabled("CPU timing data is waiting for a completed frame.");
    return;
  }

  const double frameMs = (std::max)(0.0001, GetCpuFrameMs(s.cpu));
  ImGui::Text("Frame %llu, scopes %u, frame %.3f ms",
              static_cast<unsigned long long>(s.cpu.frameIndex),
              CountValidCpuPasses(s.cpu), frameMs);
  ImGui::TextDisabled("Inclusive timings; nested scopes are not additive.");

  if (ImGui::BeginTable("CpuFrameTimingTable", 4,
                        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthFixed, 190.0f);
    ImGui::TableSetupColumn("CPU ms", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableSetupColumn("Frame Share");
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < s.cpu.passes.size(); ++i) {
      const CPU_PROFILE::PassTiming &timing = s.cpu.passes[i];
      if (!timing.valid) {
        continue;
      }

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(timing.name);
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%.3f", timing.cpuMs);
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%u", timing.callCount);
      ImGui::TableSetColumnIndex(3);
      ImGui::ProgressBar(
          static_cast<float>((std::min)(1.0, timing.cpuMs / frameMs)),
          ImVec2(-1.0f, 0.0f), "");
    }
    ImGui::EndTable();
  }
}

void DrawGpuTimingTable(const RuntimePerformanceSnapshot &s) {
  ImGui::SeparatorText("GPU Timing");
  if (!s.gpu.gpuTimingAvailable) {
    ImGui::TextDisabled("%s", s.gpu.profilerEnabled
                                  ? "Timestamp query data is waiting."
                                  : s.gpu.unavailableReason);
    return;
  }

  const double parentMs = SumGpuMsByScope(s.gpu, false);
  const double nestedSubpassMs = SumGpuMsByScope(s.gpu, true);
  ImGui::Text("Frame %llu, passes %u, parent %.3f ms, nested subpass %.3f ms",
              static_cast<unsigned long long>(s.gpu.frameIndex),
              CountValidGpuPasses(s.gpu), parentMs, nestedSubpassMs);

  if (ImGui::BeginTable("GpuDrivenTimingTable", 5,
                        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 190.0f);
    ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("GPU ms", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Ticks", ImGuiTableColumnFlags_WidthFixed, 120.0f);
    ImGui::TableSetupColumn("Share");
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < s.gpu.passes.size(); ++i) {
      const GFX::GPU_PROFILE::Pass pass =
          static_cast<GFX::GPU_PROFILE::Pass>(i);
      const GFX::GPU_PROFILE::PassTiming &timing = s.gpu.passes[i];
      if (!timing.valid) {
        continue;
      }

      const bool nestedSubpass = IsNestedGpuSubpass(pass);
      const double scopeTotal =
          (std::max)(0.0001, nestedSubpass ? nestedSubpassMs : parentMs);

      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(timing.name);
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(TimingScopeText(pass));
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%.3f", timing.gpuMs);
      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%llu", static_cast<unsigned long long>(timing.ticks));
      ImGui::TableSetColumnIndex(4);
      ImGui::ProgressBar(static_cast<float>(timing.gpuMs / scopeTotal),
                         ImVec2(-1.0f, 0.0f), "");
    }
    ImGui::EndTable();
  }
}
#endif

} // namespace HIKARI::EDITOR::PERFORMANCE_AUDIT
