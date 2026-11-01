#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Performance/HIKARI_PerformanceAuditPanel.h"

#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Material/HIKARI_GpuMaterialRegistry.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
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

#include "imgui.h"

#include "Editor/Performance/HIKARI_PerformanceAuditInternal.h"
#include <algorithm>
#include <cstdint>
#include <string>

namespace HIKARI::EDITOR::PERFORMANCE_AUDIT {

void DrawRenderPathTable(const RuntimePerformanceSnapshot& s) {
    ImGui::SeparatorText(TEXT::Localize("editor.ui.render.path.e693f1").c_str());
    if (BeginMetricTable("RenderPathMetrics", 250.0f)) {
        MetricRowText("Route", ToString(s.submission.routeMode));
        MetricRow("Mesh Forward Variants Ready / Failed", "%zu / %zu",
                  s.mesh.meshletBackend.preparedForwardVariantCount, s.mesh.meshletBackend.failedForwardVariantCount);
        MetricRow("Mainline / Strict / CPU Views Suppressed", "%s / %s / %u",
                  s.submission.gpuDrivenMainRouteActive ? "on" : "off",
                  s.gpuRegistry.strictGpuDrivenMainline ? "on" : "off", s.gpuRegistry.cpuForwardViewSuppressedCount);
        MetricRow("Scene Objects Total / Author-Visible / Hidden", "%u / %u / %u", s.scene.renderObjectCount,
                  s.scene.authorVisibleObjectCount, s.scene.hiddenObjectCount);
        MetricRow("Surfaces Total / Clustered / Dirty", "%u / %u / %u", s.scene.surfaceInstanceCount,
                  s.scene.clusteredGeometrySurfaceInstanceCount, s.scene.dirtySurfaceInstanceCount);
        MetricRow("GPU Records Source / Routed / Unsupported", "%u / %u / %u", s.gpuRegistry.sourceRecordCount,
                  s.gpuRegistry.forwardRoutedRecordCount, s.gpuRegistry.unsupportedForwardRecordCount);
        MetricRowText("GPU Scene CPU Sync", RENDER3D::GPUDRIVEN::ToString(s.gpuRegistry.syncMode));
        MetricRow("GPU Records Rebuilt This Frame", "%u", s.gpuRegistry.frameRecordBuildCount);
        MetricRow("GPU Records Reused / Hidden", "%u / %u", s.gpuRegistry.frameRecordReuseCount,
                  s.gpuRegistry.hiddenSourceRecordCount);
        if (s.gpuRegistry.syncMode == RENDER3D::GPUDRIVEN::GpuSceneSyncMode::RebuildPatchRejected) {
            MetricRowText("GPU Patch Rejection", s.gpuRegistry.patchRejection.reason);
            MetricRowText("GPU Rejected Model", s.gpuRegistry.patchRejection.modelPath.c_str());
            MetricRow("GPU Rejected Surface / Node", "%u / %u", s.gpuRegistry.patchRejection.surfaceIndex,
                      s.gpuRegistry.patchRejection.nodeIndex);
        }
        MetricRow("GPU Scene Upload Full / Dirty / Reuse", "%zu / %zu / %zu",
                  static_cast<size_t>(s.mesh.gpuSceneUpload.uploadedFullScene),
                  static_cast<size_t>(s.mesh.gpuSceneUpload.patchedDirtyRanges),
                  static_cast<size_t>(s.mesh.gpuSceneUpload.reusedResidentFrame));
        MetricRow("Joint Palette Matrix KB / Uploads / Reuse / Empty", "%.2f / %llu / %llu / %llu",
                  static_cast<double>(s.jointPaletteUpload.matrixPayloadBytes) / 1024.0,
                  static_cast<unsigned long long>(s.jointPaletteUpload.snapshotUploadCount),
                  static_cast<unsigned long long>(s.jointPaletteUpload.snapshotReuseCount),
                  static_cast<unsigned long long>(s.jointPaletteUpload.emptyBindingUploadCount));
        MetricRow("Mass Stores / Instances / Dirty Ranges", "%u / %u / %u", s.submission.massRenderStoreCount,
                  s.submission.massRenderInstanceCount, s.submission.massRenderDirtyRangeCount);
        MetricRow("Mass Templates Missing / Unsupported", "%u / %u", s.submission.massRenderMissingTemplateCount,
                  s.submission.massRenderUnsupportedTemplateCount);
        MetricRow("Mass GPU Resident / Upload Ranges / Overflow", "%zu / %zu / %zu",
                  s.mesh.gpuSceneUpload.massBufferStats.residentInstanceCount,
                  s.mesh.gpuSceneUpload.massBufferStats.uploadRangeCount,
                  s.mesh.gpuSceneUpload.massBufferStats.overflowInstanceCount);
        MetricRow("Mass GPU Upload KB / Resource Growth", "%.2f / %u",
                  static_cast<double>(s.mesh.gpuSceneUpload.massBufferStats.uploadedBytes) / 1024.0,
                  s.mesh.gpuSceneUpload.massBufferStats.resourceGrowthCount);
        MetricRow("Mass GPU Full / Dirty / Reuse", "%s / %s / %s",
                  s.mesh.gpuSceneUpload.massBufferStats.uploadedFullScene ? "yes" : "no",
                  s.mesh.gpuSceneUpload.massBufferStats.patchedDirtyRanges ? "yes" : "no",
                  s.mesh.gpuSceneUpload.massBufferStats.reusedResidentFrame ? "yes" : "no");
        MetricRow("Mass Material Visits / Missing", "%zu / %zu",
                  s.mesh.gpuSceneUpload.massBufferStats.materialBindingVisitCount,
                  s.mesh.gpuSceneUpload.massBufferStats.materialBindingMissingCount);
        MetricRow("GPU Scene Instances Opaque / DepthPre / Shadow", "%u / %u / %zu",
                  s.gpuRegistry.forwardOpaqueGpuSceneStats.instanceCount,
                  s.gpuRegistry.depthPrepassGpuSceneStats.instanceCount, s.shadow.shadowGpuSceneUploadedInstanceCount);
        MetricRow("Command Stream GPU / Traditional / Overflow", "%zu / %zu / %zu",
                  s.mesh.gpuDrivenCommandStreamGpuCommandCount, s.mesh.gpuDrivenCommandStreamTraditionalCommandCount,
                  s.mesh.traditionalCommandStream.overflowCommandCount);
        MetricRow("MaterialFX Mesh / Traditional / Blocked", "%u / %u / %u",
                  s.gpuRegistry.forwardMaterialFxMeshShaderRecordCount,
                  s.gpuRegistry.forwardMaterialFxTraditionalRecordCount,
                  s.gpuRegistry.forwardMaterialFxBlockedRecordCount);
        MetricRow("MaterialFX Shadow Mesh / Traditional", "%u / %u",
                  s.gpuRegistry.shadowMaterialFxMeshShaderRecordCount,
                  s.gpuRegistry.shadowMaterialFxTraditionalRecordCount);
        MetricRow("Skinned Mesh (Fwd / Shadow)", "%u / %u", s.gpuRegistry.forwardSkinnedMeshShaderRecordCount,
                  s.gpuRegistry.shadowSkinnedMeshShaderRecordCount);
        MetricRow("Traditional Input KB / Copies / Resident Reuse", "%.2f / %zu / %s",
                  static_cast<double>(s.mesh.traditionalCommandStream.inputUploadBytes) / 1024.0,
                  s.mesh.traditionalCommandStream.inputUploadCopyCount,
                  s.mesh.traditionalCommandStream.reusedResidentInput ? "yes" : "no");
        ImGui::EndTable();
    }
}

void DrawGpuMaterialTable(const RuntimePerformanceSnapshot& s) {
    const RENDER3D::MATERIAL::GpuMaterialRegistryStats& materials = s.gpuMaterials;
    ImGui::SeparatorText(TEXT::Localize("editor.ui.gpu.materials.d283d8").c_str());
    if (BeginMetricTable("GpuMaterialMetrics", 250.0f)) {
        MetricRow("State / Source Sync", "%s / %s", materials.initialized ? "resident" : "missing",
                  RENDER3D::MATERIAL::ToString(materials.sourceSyncMode));
        MetricRow("Slots Resident / Capacity", "%u / %u", materials.residentSlotCount, materials.capacity);
        MetricRow("Sources Bound / Records / Missing", "%u / %u / %zu", materials.sourceBindingCount,
                  s.gpuRegistry.sourceRecordCount, s.mesh.gpuSceneUpload.bufferStats.materialBindingMissingCount);
        MetricRow("Versions Data / Binding", "%llu / %llu", static_cast<unsigned long long>(materials.dataVersion),
                  static_cast<unsigned long long>(materials.bindingVersion));
        MetricRow("Resolve Requests / Reuse / Pending", "%u / %u / %u", materials.frameResolveRequestCount,
                  materials.frameSourceReuseCount, materials.pendingSourceResolveCount);
        MetricRow("Slots Created / Updated / Retired / Overflow", "%u / %u / %u / %u", materials.frameCreatedSlotCount,
                  materials.frameUpdatedSlotCount, materials.frameRetiredSlotCount, materials.frameOverflowCount);
        MetricRow("Upload Slots / Ranges / KB", "%u / %u / %.2f", materials.frameUploadedSlotCount,
                  materials.frameUploadRangeCount, static_cast<double>(materials.frameUploadBytes) / 1024.0);
        MetricRow("Binding Visits / Changed / Reused", "%zu / %zu / %zu",
                  s.mesh.gpuSceneUpload.bufferStats.materialBindingVisitCount,
                  s.mesh.gpuSceneUpload.bufferStats.materialPatchChangedCount,
                  s.mesh.gpuSceneUpload.bufferStats.materialPatchUnchangedCount);
        MetricRow("Pending Frame Copies / Stable Frames", "%u / %u", materials.pendingFrameSlotCount,
                  materials.stableFrameReuseCount);
        MetricRow("Source Sync Full / Dirty / Retry", "%u / %u / %u", materials.fullSourceSyncCount,
                  materials.incrementalSourceSyncCount, materials.retrySourceSyncCount);
        ImGui::EndTable();
    }
}

void DrawMeshShaderPipelineStatsTable(const RuntimePerformanceSnapshot& s) {
    ImGui::SeparatorText(TEXT::Localize("editor.ui.mesh.shader.pipeline.stats.b8b8b8").c_str());
    const METRIC::MetricReading* executions = METRIC::FindReading(s.metrics, METRIC::kGpuPipelineExecutions);
    if (!IsFresh(executions)) {
        // 取れない構成ではその理由を、それ以外は状態を示す。
        if (executions != nullptr && executions->status == METRIC::MetricStatus::Unsupported) {
            ImGui::TextDisabled("%s", std::string(executions->note).c_str());
        } else {
            ImGui::TextDisabled("%s", MetricText(executions, 0).c_str());
        }
        return;
    }

    const auto count = [](const METRIC::MetricReading* reading) {
        return reading != nullptr ? static_cast<uint64_t>(reading->value) : uint64_t{0};
    };
    bool drewRows = false;
    if (ImGui::BeginTable("MeshShaderPipelineStatsTable", 8,
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("AS", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("MS", ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("MS Prim", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Clip Prim", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("PS", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Prim / MS", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("PS / Prim");
        ImGui::TableHeadersRow();

        for (const PipelineStatsRow& row : PipelineStatsRows(s.metrics)) {
            if (!IsFresh(row.calls)) {
                continue;
            }
            drewRows = true;

            const uint64_t msInvocations = count(row.msInvocations);
            const uint64_t msPrimitives = count(row.msPrimitives);
            const uint64_t psInvocations = count(row.psInvocations);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.name.data(), row.name.data() + row.name.size());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%llu", static_cast<unsigned long long>(count(row.asInvocations)));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%llu", static_cast<unsigned long long>(msInvocations));
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%llu", static_cast<unsigned long long>(msPrimitives));
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%llu", static_cast<unsigned long long>(count(row.clipPrimitives)));
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%llu", static_cast<unsigned long long>(psInvocations));
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%.2f", SafeDivide(msPrimitives, msInvocations));
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%.2f", SafeDivide(psInvocations, msPrimitives));
        }
        ImGui::EndTable();
    }
    if (!drewRows) {
        ImGui::TextDisabled(
            TEXT::Localize("editor.ui.no.meshlet.draw.pipeline.statistics.were.captured.in.the.latest..e97211")
                .c_str());
    }
}

void DrawGeometryPipelineTable(const RuntimePerformanceSnapshot& s) {
    ImGui::SeparatorText(TEXT::Localize("editor.ui.geometry.pipeline.9eac28").c_str());
    if (BeginMetricTable("GeometryPipelineMetrics", 250.0f)) {
        MetricRow("Cluster Cull Source / Submitted / Overflow", "%zu / %zu / %zu",
                  s.mesh.clusterGpuCull.sourceInstanceCount, s.mesh.clusterGpuCull.submittedInstanceCount,
                  s.mesh.clusterGpuCull.overflowInstanceCount);
        const bool asFineCull = s.mesh.clusterGpuCull.meshletFineCullingDeferredToAmplificationShader;
        MetricRow("Meshlet Fine Cull Owner / Coarse Owner", "%s / CS page + HZB", asFineCull ? "AS" : "CS");
        MetricRow(asFineCull ? "Candidate Ranges / Meshlets / Dispatch Args" : "Visible Ranges / Clusters / Draw Args",
                  "%u / %u / %u", s.mesh.clusterGpuCull.gpuVisibleRangeCount,
                  s.mesh.clusterGpuCull.gpuVisibleClusterCount, s.mesh.clusterGpuCull.gpuDrawCommandCount);
        MetricRow("Culling Camera Frozen / HZB Published", "%s / %s",
                  s.mesh.gpuDrivenCullingCameraFrozen ? "yes" : "no",
                  s.depthVisibility.visibilityUsedHzb ? "yes" : "no");
        MetricRow("HZB Visibility / Pyramid / History", "%s / %s / %s %s",
                  RENDER3D::SCREENSPACE::ToString(s.depthVisibility.visibilitySource),
                  RENDER3D::SCREENSPACE::ToString(s.depthVisibility.latestPyramidSource),
                  s.depthVisibility.historyReady ? "ready" : "cold",
                  s.depthVisibility.historyMatched ? "matched" : "moved");
        MetricRow("CS HZB Enabled / Size / Budget Skipped", "%s / %u x %u / %u",
                  s.mesh.clusterGpuCull.hzbOcclusionEnabled ? "yes" : "no", s.mesh.clusterGpuCull.hzbOcclusionWidth,
                  s.mesh.clusterGpuCull.hzbOcclusionHeight, s.mesh.clusterGpuCull.gpuHzbBudgetSkippedCount);
        const auto& visibility = s.mesh.meshletBackend.visibility;
        MetricRow("Scene HZB Builds / Aux Depth Reused", "%u / %s", s.depthVisibility.sceneDepthPyramidBuildCount,
                  s.depthVisibility.auxiliaryDepthReused ? "yes" : "no");
        MetricRow("AS Forward HZB Bound / Size / Mips", "%s / %u x %u / %u", visibility.forwardHzbBound ? "yes" : "no",
                  visibility.width, visibility.height, visibility.mipCount);
        for (size_t phase = 0; phase < visibility.samples.size(); ++phase) {
            const auto& sample = visibility.samples[phase];
            MetricRow(phase == 0 ? "AS Aux Sample / Retire Fence" : "AS Forward Sample / Retire Fence", "%s / %llu",
                      sample.valid ? "ready (1/16 frames)" : "pending",
                      static_cast<unsigned long long>(sample.retireFence));
            if (!sample.valid)
                continue;
            const auto& c = sample.counters;
            MetricRow(phase == 0 ? "AS Aux Candidates / Submitted / Invalid"
                                 : "AS Forward Candidates / Submitted / Invalid",
                      "%u / %u / %u", c.candidates, c.submitted, c.invalid);
            MetricRow(phase == 0 ? "AS Aux Frustum / Cone Culled / Unbounded"
                                 : "AS Forward Frustum / Cone Culled / Unbounded",
                      "%u / %u / %u", c.frustumCulled, c.coneCulled, c.unbounded);
            MetricRow(phase == 0 ? "AS Aux Cone Tested / HZB Tested / Culled"
                                 : "AS Forward Cone Tested / HZB Tested / Culled",
                      "%u / %u / %u", c.coneTested, c.hzbTested, c.hzbCulled);
        }
        MetricRow("CS HZB Page Tested / Culled | Cluster Tested / Culled", "%u / %u | %u / %u",
                  s.mesh.clusterGpuCull.gpuPageOcclusionTestedCount, s.mesh.clusterGpuCull.gpuPageOcclusionCulledCount,
                  s.mesh.clusterGpuCull.gpuClusterOcclusionTestedCount,
                  s.mesh.clusterGpuCull.gpuClusterOcclusionCulledCount);
        MetricRow("HZB Raw / Confirmed / Pending / Reset", "%u / %u / %u / %u",
                  s.mesh.clusterGpuCull.gpuHzbRawOccludedCount, s.mesh.clusterGpuCull.gpuHzbTemporalConfirmedCount,
                  s.mesh.clusterGpuCull.gpuHzbTemporalPendingCount, s.mesh.clusterGpuCull.gpuHzbTemporalResetCount);
        MetricRow("GPU LOD Selected L0 / L1 / L2 / L3+", "%u / %u / %u / %u",
                  s.mesh.clusterGpuCull.gpuLod0SelectedCount, s.mesh.clusterGpuCull.gpuLod1SelectedCount,
                  s.mesh.clusterGpuCull.gpuLod2SelectedCount, s.mesh.clusterGpuCull.gpuLod3PlusSelectedCount);
        MetricRow("CS Double Sided Candidates / Draw Args / Cone Skips", "%u / %u / %u",
                  s.mesh.clusterGpuCull.gpuDoubleSidedClusterCount,
                  s.mesh.clusterGpuCull.gpuDoubleSidedDrawCommandCount,
                  s.mesh.clusterGpuCull.gpuConeSkippedDoubleSidedCount);
        MetricRow("LOD Policy Error / Radius Relax / Error Relax", "%.4f / %.2f / %.2f",
                  s.mesh.clusterGpuCull.lodTargetErrorNdc, s.mesh.clusterGpuCull.lodTransitionRelaxPerLevel,
                  s.mesh.clusterGpuCull.lodErrorRelaxPerLevel);
        MetricRow("Meshlet Dispatch Requested / Submitted / Calls / Empty", "%zu / %zu / %zu / %zu",
                  s.mesh.meshletBackend.requestedDispatchCount, s.mesh.meshletBackend.submittedDispatchCount,
                  s.mesh.meshletBackend.submitCallCount, s.mesh.meshletBackend.skippedBucketCount);
        MetricRow("Meshlet Passes Forward / DepthPre / Shadow / Geometry", "%zu / %zu / %zu / %zu",
                  s.mesh.meshletBackend.forwardSubmittedDispatchCount,
                  s.mesh.meshletBackend.depthPrepassSubmittedDispatchCount,
                  s.mesh.meshletBackend.shadowSubmittedDispatchCount,
                  s.mesh.meshletBackend.geometryAuxSubmittedDispatchCount);
        ImGui::EndTable();
    }
}

} // namespace HIKARI::EDITOR::PERFORMANCE_AUDIT
