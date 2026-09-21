#include "Core/Text/HIKARI_Localization.h"
#include "HIKARI_PerformanceAuditPanel.h"

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

namespace HIKARI {

using namespace EDITOR::PERFORMANCE_AUDIT;

void PerformanceAuditPanel::Draw(bool &open) const {
#if defined(HIKARI_WITH_EDITOR)
  if (!open) {
    return;
  }
  if (!ImGui::Begin(TEXT::LocalizeWindow("editor.ui.performance.audit.e54bcf", "Performance Audit").c_str(), &open)) {
    ImGui::End();
    return;
  }

  DrawContents();

  ImGui::End();
#else
  (void)open;
#endif
}

void PerformanceAuditPanel::DrawContents() const {
#if defined(HIKARI_WITH_EDITOR)
  const RuntimePerformanceSnapshot snapshot = BuildSnapshot();
  if (ImGui::Button(TEXT::LocalizeWindow("editor.copyDiagnostics", "診断結果をコピー").c_str())) {
    const auto report = BuildPerformanceReport(snapshot);
    ImGui::SetClipboardText(report.c_str());
  }
  ImGui::SameLine();
  ImGui::TextDisabled(TEXT::Localize("editor.captureStatsHint").c_str());
  DrawFrameSummary(snapshot);
  DrawReadiness(snapshot);
  DrawCpuTimingTable(snapshot);
  DrawGpuTimingTable(snapshot);
  DrawMeshShaderPipelineStatsTable(snapshot);
  DrawRenderPathTable(snapshot);
  DrawGpuMaterialTable(snapshot);
  DrawGeometryPipelineTable(snapshot);
  DrawTemporalTable(snapshot);
  DrawEffectsTable(snapshot);
  DrawResourceSummaryTable(snapshot);
#endif
}

} // namespace HIKARI
