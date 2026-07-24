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
void DrawRenderPathTable(const RuntimePerformanceSnapshot &s) {
  ImGui::SeparatorText("Render Path");
  if (BeginMetricTable("RenderPathMetrics", 250.0f)) {
    MetricRowText("Route", ToString(s.submission.routeMode));
    MetricRow("Mainline / Strict / CPU Views Suppressed", "%s / %s / %u",
              s.submission.gpuDrivenMainRouteActive ? "on" : "off",
              s.gpuRegistry.strictGpuDrivenMainline ? "on" : "off",
              s.gpuRegistry.cpuForwardViewSuppressedCount);
    MetricRow("Scene Scanned / Submitted / Hidden", "%d / %d / %d",
              s.submission.scannedModelCount, s.submission.submittedModelCount,
              s.submission.hiddenModelCount);
    MetricRow("Surfaces Total / Clustered / Dirty", "%u / %u / %u",
              s.scene.surfaceInstanceCount,
              s.scene.clusteredGeometrySurfaceInstanceCount,
              s.scene.dirtySurfaceInstanceCount);
    MetricRow("GPU Records Source / Routed / Unsupported", "%u / %u / %u",
              s.gpuRegistry.sourceRecordCount,
              s.gpuRegistry.forwardRoutedRecordCount,
              s.gpuRegistry.unsupportedForwardRecordCount);
    MetricRow("GPU Scene Upload Full / Dirty / Reuse", "%zu / %zu / %zu",
              s.mesh.surfaceGpuSceneFullUploadCount,
              s.mesh.surfaceGpuSceneDirtyPatchCount,
              s.mesh.surfaceGpuSceneReuseCount);
    MetricRow("GPU Scene Instances Opaque / DepthPre / Shadow", "%u / %u / %zu",
              s.gpuRegistry.forwardOpaqueGpuSceneStats.instanceCount,
              s.gpuRegistry.depthPrepassGpuSceneStats.instanceCount,
              s.shadow.shadowGpuSceneUploadedInstanceCount);
    MetricRow("Command Stream GPU / Traditional / Overflow", "%zu / %zu / %zu",
              s.mesh.gpuDrivenCommandStreamGpuCommandCount,
              s.mesh.gpuDrivenCommandStreamTraditionalCommandCount,
              s.mesh.traditionalCommandStreamOverflowCommandCount);
    MetricRow("MaterialFX Mesh / Traditional / Blocked", "%u / %u / %u",
              s.gpuRegistry.forwardMaterialFxMeshShaderRecordCount,
              s.gpuRegistry.forwardMaterialFxTraditionalRecordCount,
              s.gpuRegistry.forwardMaterialFxBlockedRecordCount);
    MetricRow("MaterialFX Shadow Mesh / Traditional", "%u / %u",
              s.gpuRegistry.shadowMaterialFxMeshShaderRecordCount,
              s.gpuRegistry.shadowMaterialFxTraditionalRecordCount);
    MetricRow("Skinned Mesh / Traditional (Fwd / Shadow)", "%u / %u | %u / %u",
              s.gpuRegistry.forwardSkinnedMeshShaderRecordCount,
              s.gpuRegistry.forwardSkinnedTraditionalRecordCount,
              s.gpuRegistry.shadowSkinnedMeshShaderRecordCount,
              s.gpuRegistry.shadowSkinnedTraditionalRecordCount);
    MetricRow(
        "Traditional Input KB / Copies / Resident Reuse", "%.2f / %zu / %s",
        static_cast<double>(s.mesh.traditionalCommandStreamInputUploadBytes) /
            1024.0,
        s.mesh.traditionalCommandStreamInputUploadCopyCount,
        s.mesh.traditionalCommandStreamResidentInputReuseCount != 0u ? "yes"
                                                                     : "no");
    ImGui::EndTable();
  }
}

void DrawGpuMaterialTable(const RuntimePerformanceSnapshot &s) {
  const RENDER3D::MATERIAL::GpuMaterialRegistryStats &materials =
      s.gpuMaterials;
  ImGui::SeparatorText("GPU Materials");
  if (BeginMetricTable("GpuMaterialMetrics", 250.0f)) {
    MetricRow("State / Source Sync", "%s / %s",
              materials.initialized ? "resident" : "missing",
              RENDER3D::MATERIAL::ToString(materials.sourceSyncMode));
    MetricRow("Slots Resident / Capacity", "%u / %u",
              materials.residentSlotCount, materials.capacity);
    MetricRow("Sources Bound / Records / Missing", "%u / %u / %zu",
              materials.sourceBindingCount, s.gpuRegistry.sourceRecordCount,
              s.mesh.surfaceGpuSceneMaterialPatchFailCount);
    MetricRow("Versions Data / Binding", "%llu / %llu",
              static_cast<unsigned long long>(materials.dataVersion),
              static_cast<unsigned long long>(materials.bindingVersion));
    MetricRow("Resolve Requests / Reuse / Pending", "%u / %u / %u",
              materials.frameResolveRequestCount,
              materials.frameSourceReuseCount,
              materials.pendingSourceResolveCount);
    MetricRow("Slots Created / Updated / Retired / Overflow",
              "%u / %u / %u / %u", materials.frameCreatedSlotCount,
              materials.frameUpdatedSlotCount, materials.frameRetiredSlotCount,
              materials.frameOverflowCount);
    MetricRow("Upload Slots / Ranges / KB", "%u / %u / %.2f",
              materials.frameUploadedSlotCount, materials.frameUploadRangeCount,
              static_cast<double>(materials.frameUploadBytes) / 1024.0);
    MetricRow("Binding Visits / Changed / Reused", "%zu / %zu / %zu",
              s.mesh.surfaceGpuSceneMaterialPatchCount,
              s.mesh.surfaceGpuSceneMaterialPatchChangedCount,
              s.mesh.surfaceGpuSceneMaterialPatchUnchangedCount);
    MetricRow("Pending Frame Copies / Stable Frames", "%u / %u",
              materials.pendingFrameSlotCount, materials.stableFrameReuseCount);
    MetricRow("Source Sync Full / Dirty / Retry", "%u / %u / %u",
              materials.fullSourceSyncCount,
              materials.incrementalSourceSyncCount,
              materials.retrySourceSyncCount);
    ImGui::EndTable();
  }
}

void DrawMeshShaderPipelineStatsTable(const RuntimePerformanceSnapshot &s) {
  ImGui::SeparatorText("Mesh Shader Pipeline Stats");
  if (!s.pipelineStats.pipelineStatsAvailable) {
    const char *reason = s.pipelineStats.unavailableReason != nullptr &&
                                 s.pipelineStats.unavailableReason[0] != '\0'
                             ? s.pipelineStats.unavailableReason
                             : "Pipeline statistics query data is waiting.";
    ImGui::TextDisabled("%s", reason);
    return;
  }

  bool drewRows = false;
  if (ImGui::BeginTable("MeshShaderPipelineStatsTable", 8,
                        ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 190.0f);
    ImGui::TableSetupColumn("AS", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("MS", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("MS Prim", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Clip Prim", ImGuiTableColumnFlags_WidthFixed,
                            90.0f);
    ImGui::TableSetupColumn("PS", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Prim / MS", ImGuiTableColumnFlags_WidthFixed,
                            90.0f);
    ImGui::TableSetupColumn("PS / Prim");
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < s.pipelineStats.passes.size(); ++i) {
      const GFX::GPU_PROFILE::Pass pass =
          static_cast<GFX::GPU_PROFILE::Pass>(i);
      if (!IsMeshletPipelineStatsPass(pass)) {
        continue;
      }

      const GFX::GPU_PIPELINE_STATS::PassPipelineStats &stats =
          s.pipelineStats.passes[i];
      if (!stats.valid) {
        continue;
      }
      drewRows = true;

      const D3D12_QUERY_DATA_PIPELINE_STATISTICS1 &c = stats.counters;
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(stats.name);
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%llu", static_cast<unsigned long long>(c.ASInvocations));
      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%llu", static_cast<unsigned long long>(c.MSInvocations));
      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%llu", static_cast<unsigned long long>(c.MSPrimitives));
      ImGui::TableSetColumnIndex(4);
      ImGui::Text("%llu", static_cast<unsigned long long>(c.CPrimitives));
      ImGui::TableSetColumnIndex(5);
      ImGui::Text("%llu", static_cast<unsigned long long>(c.PSInvocations));
      ImGui::TableSetColumnIndex(6);
      ImGui::Text("%.2f", SafeDivide(c.MSPrimitives, c.MSInvocations));
      ImGui::TableSetColumnIndex(7);
      ImGui::Text("%.2f", SafeDivide(c.PSInvocations, c.MSPrimitives));
    }
    ImGui::EndTable();
  }
  if (!drewRows) {
    ImGui::TextDisabled("No meshlet draw pipeline statistics were captured in "
                        "the latest resolved frame.");
  }
}

void DrawGeometryPipelineTable(const RuntimePerformanceSnapshot &s) {
  ImGui::SeparatorText("Geometry Pipeline");
  if (BeginMetricTable("GeometryPipelineMetrics", 250.0f)) {
    MetricRow("Cluster Cull Source / Submitted / Overflow", "%zu / %zu / %zu",
              s.mesh.clusterGpuCullSourceInstanceCount,
              s.mesh.clusterGpuCullSubmittedInstanceCount,
              s.mesh.clusterGpuCullOverflowInstanceCount);
    const bool asFineCull =
        s.mesh.clusterGpuCullFineCullingOwnedByAmplificationShader;
    MetricRow("Meshlet Fine Cull Owner / Coarse Owner", "%s / CS page + HZB",
              asFineCull ? "AS" : "CS");
    MetricRow(asFineCull ? "Candidate Ranges / Meshlets / Dispatch Args"
                         : "Visible Ranges / Clusters / Draw Args",
              "%zu / %zu / %zu", s.mesh.clusterGpuCullGpuVisibleRangeCount,
              s.mesh.clusterGpuCullGpuVisibleClusterCount,
              s.mesh.clusterGpuCullGpuDrawCommandCount);
    MetricRow("Culling Camera Frozen / HZB Used", "%s / %s",
              s.mesh.gpuDrivenCullingCameraFrozen ? "yes" : "no",
              s.depthVisibility.visibilityUsedHzb ? "yes" : "no");
    MetricRow(
        "HZB Visibility / Pyramid / History", "%s / %s / %s %s",
        RENDER3D::SCREENSPACE::ToString(s.depthVisibility.visibilitySource),
        RENDER3D::SCREENSPACE::ToString(s.depthVisibility.latestPyramidSource),
        s.depthVisibility.historyReady ? "ready" : "cold",
        s.depthVisibility.historyMatched ? "matched" : "moved");
    MetricRow("HZB Enabled / Size / Budget Skipped", "%s / %zu x %zu / %zu",
              s.mesh.clusterGpuCullHzbOcclusionEnabled ? "yes" : "no",
              s.mesh.clusterGpuCullHzbOcclusionWidth,
              s.mesh.clusterGpuCullHzbOcclusionHeight,
              s.mesh.clusterGpuCullGpuHzbBudgetSkippedCount);
    MetricRow("HZB Page Tested / Culled | Cluster Tested / Culled",
              "%zu / %zu | %zu / %zu",
              s.mesh.clusterGpuCullGpuPageOcclusionTestedCount,
              s.mesh.clusterGpuCullGpuPageOcclusionCulledCount,
              s.mesh.clusterGpuCullGpuClusterOcclusionTestedCount,
              s.mesh.clusterGpuCullGpuClusterOcclusionCulledCount);
    MetricRow("HZB Raw / Confirmed / Pending / Reset", "%zu / %zu / %zu / %zu",
              s.mesh.clusterGpuCullGpuHzbRawOccludedCount,
              s.mesh.clusterGpuCullGpuHzbTemporalConfirmedCount,
              s.mesh.clusterGpuCullGpuHzbTemporalPendingCount,
              s.mesh.clusterGpuCullGpuHzbTemporalResetCount);
    MetricRow("GPU LOD Selected L0 / L1 / L2 / L3+", "%zu / %zu / %zu / %zu",
              s.mesh.clusterGpuCullGpuLod0SelectedCount,
              s.mesh.clusterGpuCullGpuLod1SelectedCount,
              s.mesh.clusterGpuCullGpuLod2SelectedCount,
              s.mesh.clusterGpuCullGpuLod3PlusSelectedCount);
    MetricRow("Double Sided Clusters / Draw Args / Cone Skips",
              "%zu / %zu / %zu",
              s.mesh.clusterGpuCullGpuDoubleSidedClusterCount,
              s.mesh.clusterGpuCullGpuDoubleSidedDrawCommandCount,
              s.mesh.clusterGpuCullGpuConeSkippedDoubleSidedCount);
    MetricRow("LOD Policy Error / Radius Relax / Error Relax",
              "%.4f / %.2f / %.2f", s.mesh.clusterGpuCullLodTargetErrorNdc,
              s.mesh.clusterGpuCullLodTransitionRelaxPerLevel,
              s.mesh.clusterGpuCullLodErrorRelaxPerLevel);
    MetricRow("Meshlet Dispatch Requested / Submitted / Calls / Empty",
              "%zu / %zu / %zu / %zu",
              s.mesh.meshletBackendRequestedDispatchCount,
              s.mesh.meshletBackendSubmittedDispatchCount,
              s.mesh.meshletBackendSubmitCallCount,
              s.mesh.meshletBackendSkippedBucketCount);
    MetricRow("Meshlet Passes Forward / DepthPre / Shadow / Geometry",
              "%zu / %zu / %zu / %zu",
              s.mesh.meshletBackendForwardSubmittedDispatchCount,
              s.mesh.meshletBackendDepthPrepassSubmittedDispatchCount,
              s.mesh.meshletBackendShadowSubmittedDispatchCount,
              s.mesh.meshletBackendGeometryAuxSubmittedDispatchCount);
    ImGui::EndTable();
  }
}
#endif

} // namespace HIKARI::EDITOR::PERFORMANCE_AUDIT
