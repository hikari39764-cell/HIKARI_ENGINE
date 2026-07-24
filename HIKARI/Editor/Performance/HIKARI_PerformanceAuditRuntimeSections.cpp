#include "Editor/Performance/HIKARI_PerformanceAuditPanel.h"

#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_GpuPipelineStatsProfiler.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironmentNames.h"
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
void DrawResourceSummaryTable(const RuntimePerformanceSnapshot &s) {
  ImGui::SeparatorText("Resources");
  if (BeginMetricTable("ClusterResourceMetrics")) {
    MetricRow("Context / Ready / Failed", "%s / %u / %u",
              s.clusterResources.initialized ? "Ready" : "Missing",
              s.clusterResources.readyResourceCount,
              s.clusterResources.failedCount);
    MetricRow("Requests / Hits / Misses / Loaded", "%u / %u / %u / %u",
              s.clusterResources.requestCount, s.clusterResources.hitCount,
              s.clusterResources.missCount, s.clusterResources.loadedCount);
    MetricRow("Surfaces / LOD Ranges / Clusters / Pages / Ranges",
              "%u / %u / %u / %u / %u", s.clusterResources.surfaceCount,
              s.clusterResources.surfaceLodRangeCount,
              s.clusterResources.clusterCount, s.clusterResources.pageCount,
              s.clusterResources.surfaceRangeCount);
    MetricRow("Vertices / Indices / Primitives / GPU Bytes",
              "%u / %u / %u / %.2f MB", s.clusterResources.vertexCount,
              s.clusterResources.indexCount,
              s.clusterResources.meshletPrimitiveCount,
              static_cast<double>(s.clusterResources.gpuBufferBytes) /
                  (1024.0 * 1024.0));
    MetricRow("Shader SRV / Missing / Allocation Failed", "%u / %u / %u",
              s.clusterResources.shaderVisibleResourceCount,
              s.clusterResources.missingDescriptorCount,
              s.clusterResources.descriptorAllocationFailedCount);
    MetricRow("Descriptor Pool Used / Capacity / Failed", "%u / %u / %u",
              s.descriptorPool.used, s.descriptorPool.capacity,
              s.descriptorPool.failedAllocationCount);
    ImGui::EndTable();
  }
}

void DrawEffectsTable(const RuntimePerformanceSnapshot &s) {
  ImGui::SeparatorText("Effects");
  if (BeginMetricTable("EffectsMetrics", 250.0f)) {
    MetricRow("SSAO Mode / Valid / Half", "%s / %s / %s",
              GetSsaoModeName(s.ssao.mode),
              s.ssao.valid ? "yes" : "no",
              s.ssao.halfResolution ? "yes" : "no");
    MetricRow("SSAO AO Size / Samples / Blur", "%u x %u / %u / %u",
              s.ssao.internalWidth, s.ssao.internalHeight, s.ssao.sampleCount,
              s.ssao.blurIterations);
    MetricRow("SSAO CPU Main / Blur / Total", "%.3f / %.3f / %.3f ms",
              s.ssao.mainCpuMs, s.ssao.blurCpuMs, s.ssao.totalCpuMs);
    MetricRow("Volumetric Requested / Active / History", "%s / %s / %s",
              s.volumetric.requested ? "yes" : "no",
              s.volumetric.active ? "yes" : "no",
              s.volumetric.historyValid ? "valid" : "cold");
    MetricRow("Volumetric Quality / Froxels / Tile",
              "%s / %u x %u x %u / %u px",
              RENDER3D::VolumetricLightingQualityLabel(
                  static_cast<RENDER3D::VolumetricLightingQuality>(
                      s.volumetric.qualityLevel)),
              s.volumetric.froxelWidth, s.volumetric.froxelHeight,
              s.volumetric.froxelDepth, s.volumetric.froxelPixelSize);
    MetricRow("Volumetric Working Set", "%.2f MB",
              static_cast<double>(s.volumetric.workingSetBytes) /
                  (1024.0 * 1024.0));
    MetricRow("Volumetric Shadow / Points / History Weight", "%s / %u / %.2f",
              s.volumetric.shadowed ? "yes" : "no",
              s.volumetric.pointLightCount, s.volumetric.temporalWeight);
    MetricRow("Volumetric Dispatch / Resizes / Resets", "%u / %llu / %llu",
              s.volumetric.dispatchCount,
              static_cast<unsigned long long>(s.volumetric.resizeCount),
              static_cast<unsigned long long>(s.volumetric.historyResetCount));
    MetricRow("Shadow Enabled / Resolution / Ortho / Texel",
              "%s / %u / %.2f / %.5f", s.shadow.enabled ? "yes" : "no",
              s.shadow.resolution, s.shadow.orthoSize, s.shadow.worldTexelSize);
    const std::string currentMissReason =
        ShadowCacheMissReasonText(s.shadow.shadowCacheMissReasonFlags);
    const std::string lastMissReason =
        ShadowCacheMissReasonText(s.shadow.shadowCacheLastMissReasonFlags);
    MetricRow("Shadow Cache Valid / Hit / Miss / Last", "%s / %s / %s / %s",
              s.shadow.shadowCacheValid ? "yes" : "no",
              s.shadow.shadowCacheHit ? "yes" : "no", currentMissReason.c_str(),
              lastMissReason.c_str());
    MetricRow("Shadow Cache Hits / Misses / Copy / Update",
              "%zu / %zu / %zu / %zu", s.shadow.shadowCacheHitCount,
              s.shadow.shadowCacheMissCount,
              s.shadow.shadowStaticCacheCopyCount,
              s.shadow.shadowStaticCacheUpdateCount);
    MetricRow("Shadow Sources Static / Dynamic / Drawn",
              "%zu / %zu / %s %s %s %s",
              s.shadow.shadowStaticSourceInstanceCount,
              s.shadow.shadowDynamicSourceInstanceCount,
              s.shadow.shadowStaticRendered ? "static" : "-",
              s.shadow.shadowDynamicRendered ? "dynamic" : "-",
              s.shadow.shadowUnifiedRendered ? "unified" : "-",
              s.shadow.shadowFallbackRendered ? "fallback" : "-");
    MetricRow("Shadow Meshlet Dispatch Req / Submitted / Skip",
              "%zu / %zu / %zu", s.shadow.shadowMeshletRequestedDispatchCount,
              s.shadow.shadowMeshletSubmittedDispatchCount,
              s.shadow.shadowMeshletSkippedDispatchCount);
    ImGui::EndTable();
  }
}

void DrawTemporalTable(const RuntimePerformanceSnapshot &s) {
  ImGui::SeparatorText("Temporal");
  if (BeginMetricTable("TemporalMetrics", 250.0f)) {
    const RENDER3D::RenderQualitySettings &renderQuality =
        RENDER3D::GetRenderQualitySettings();
    MetricRow(
        "AA Mode", "%s",
        RENDER3D::RenderAntiAliasingModeLabel(renderQuality.antiAliasingMode));
    MetricRow(
        "Frame Generation Policy / Multiplier", "%s / %ux",
        RENDER3D::RenderFrameGenerationModeLabel(
            renderQuality.frameGenerationMode),
        static_cast<unsigned int>(renderQuality.frameGenerationMultiplier));
    MetricRow("Frame / History / Reset", "%llu / %s / %s",
              static_cast<unsigned long long>(s.temporalFrame.frameIndex),
              s.temporalFrame.historyValid ? "valid" : "cold",
              RENDER3D::TEMPORAL::ToString(s.temporalFrame.resetReason));
    MetricRow("Render / Output Size", "%u x %u / %u x %u",
              s.temporalFrame.renderWidth, s.temporalFrame.renderHeight,
              s.temporalFrame.outputWidth, s.temporalFrame.outputHeight);
    MetricRow("Presentation H / UI / Final", "%s / %s / %s",
              s.presentation.hudlessReady ? "ready" : "missing",
              s.presentation.uiReady ? "ready" : "missing",
              s.presentation.finalReady ? "ready" : "missing");
    MetricRow("Presentation Size / FG Inputs", "%d x %d / %s",
              s.presentation.width, s.presentation.height,
              s.presentation.HasFrameGenerationInputs() ? "ready" : "missing");
    MetricRow("Presentation UI Logical", "%d x %d",
              s.presentation.uiLogicalWidth, s.presentation.uiLogicalHeight);
    MetricRow("Jitter / Phase / Pixels", "%s / %u / %.3f, %.3f",
              s.temporalFrame.jitterEnabled ? "on" : "off",
              s.temporalFrame.jitterPhase, s.temporalFrame.camera.jitter.x,
              s.temporalFrame.camera.jitter.y);
    MetricRow("Motion RG / Metadata / Written", "%s / %s / %s",
              s.temporalResources.motionVectorReady ? "yes" : "no",
              s.temporalResources.motionMetadataReady ? "yes" : "no",
              s.temporalResources.motionVectorWritten ? "yes" : "no");
    MetricRow("Velocity Draws Rigid / Skinned / AlphaMask", "%u / %u / %u",
              s.temporalResources.rigidVelocityDrawCount,
              s.temporalResources.skinnedVelocityDrawCount,
              s.temporalResources.alphaMaskedVelocityDrawCount);
    MetricRow("Scene Color / TAA", "%s / %s / %s",
              s.temporalResources.sceneColorReady ? "yes" : "no",
              s.temporalResources.taaEnabled ? "on" : "off",
              s.temporalResources.taaResolved ? "resolved" : "skipped");
    MetricRow("History Color / Depth / Valid", "%s / %s / %s",
              s.temporalResources.historyColorReady ? "yes" : "no",
              s.temporalResources.historyDepthReady ? "yes" : "no",
              (s.temporalResources.historyColorValid &&
               s.temporalResources.historyDepthValid)
                  ? "yes"
                  : "no");
    MetricRow("TAA Resolved Ready", "%s",
              s.temporalResources.taaResolvedColorReady ? "yes" : "no");
    MetricRow("Exposure Ready / Written", "%s / %s",
              s.temporalResources.exposureReady ? "yes" : "no",
              s.temporalResources.exposureWritten ? "yes" : "no");
    MetricRow("Masks R/T/Invalid Ready / Valid / Source", "%s / %s / %s",
              (s.temporalResources.reactiveMaskReady &&
               s.temporalResources.transparencyMaskReady &&
               s.temporalResources.invalidDepthMotionMaskReady)
                  ? "yes"
                  : "no",
              s.temporalResources.masksWritten ? "yes" : "no",
              s.temporalResources.masksCompositionDerived ? "composition-diff"
                                                          : "opaque-zero");
    MetricRow("Streamline SDK / Init / Device", "%s / %s / %s",
              s.streamline.sdkCompiled ? "yes" : "no",
              s.streamline.initialized ? "yes" : "no",
              s.streamline.deviceAttached ? "yes" : "no");
    MetricRow("SL Plugins DLSS / FG / Reflex / PCL", "%s / %s / %s / %s",
              s.streamline.dlssPluginPresent ? "yes" : "no",
              s.streamlineFrameGeneration.pluginPresent ? "yes" : "no",
              s.streamlineReflex.reflexPluginPresent ? "yes" : "no",
              s.streamlineReflex.pclPluginPresent ? "yes" : "no");
    MetricRow("DLSS-G Loaded / Supported", "%s / %s",
              s.streamlineFrameGeneration.featureLoaded ? "yes" : "no",
              s.streamlineFrameGeneration.supported ? "yes" : "no");
    MetricRow("DLSS Support / Token / Constants", "%s / %s / %s",
              s.streamline.dlssSupported ? "yes" : "no",
              s.streamline.frameTokenReady ? "yes" : "no",
              s.streamline.constantsSubmitted ? "yes" : "no");
    MetricRow("DLSS Mode / Requested / Evaluated", "%s / %s / %s",
              RENDER3D::UPSCALING::ToString(s.streamline.mode),
              s.streamline.dlssRequested ? "yes" : "no",
              s.streamline.dlssEvaluated ? "yes" : "no");
    MetricRow("DLSS Fallback / Output / Evaluations", "%s / %s / %llu",
              s.streamline.fallbackUsed ? "yes" : "no",
              s.streamline.outputReady ? "ready" : "missing",
              static_cast<unsigned long long>(s.streamline.evaluationCount));
    MetricRow("DLSS Failures / VRAM MB", "%llu / %.2f",
              static_cast<unsigned long long>(s.streamline.failureCount),
              static_cast<double>(s.streamline.estimatedVramBytes) /
                  (1024.0 * 1024.0));
    MetricRow("DLSS Render / Output", "%u x %u / %u x %u",
              s.streamline.renderWidth, s.streamline.renderHeight,
              s.streamline.outputWidth, s.streamline.outputHeight);
    MetricRow("DLSS Optimal / Min / Max", "%u x %u / %u x %u / %u x %u",
              s.streamline.optimalRenderWidth, s.streamline.optimalRenderHeight,
              s.streamline.minRenderWidth, s.streamline.minRenderHeight,
              s.streamline.maxRenderWidth, s.streamline.maxRenderHeight);
    MetricRow(
        "Streamline Status / Last", "%s / %s: %s",
        RENDER3D::UPSCALING::ToString(s.streamline.status),
        s.streamline.lastOperation.empty() ? "none"
                                           : s.streamline.lastOperation.c_str(),
        s.streamline.lastResult.empty() ? "none"
                                        : s.streamline.lastResult.c_str());
    MetricRow("DLSS Retry / Consecutive / Frame", "%s / %u / %llu",
              s.streamline.retryPending ? "cooldown" : "ready",
              s.streamline.consecutiveFailureCount,
              static_cast<unsigned long long>(s.streamline.retryFrameIndex));
    MetricRow("Reflex Support / Options / Sleep", "%s / %s / %s",
              s.streamlineReflex.reflexSupported ? "yes" : "no",
              s.streamlineReflex.optionsConfigured ? "ready" : "missing",
              s.streamlineReflex.sleepCalled ? "yes" : "no");
    MetricRow("Reflex PCL / Markers / Failures", "%s / 0x%02X / %llu",
              s.streamlineReflex.pclSupported ? "yes" : "no",
              s.streamlineReflex.markerMask,
              static_cast<unsigned long long>(s.streamlineReflex.failureCount));
    MetricRow("Frame Gen Support / Host / Inputs", "%s / %s / %s",
              s.streamlineFrameGeneration.supported ? "yes" : "no",
              s.streamlineFrameGeneration.hostAllowed ? "game" : "editor-off",
              s.streamlineFrameGeneration.inputsReady ? "ready" : "missing");
    MetricRow("Frame Gen Requested / BackBuffer / Inputs", "%s / %s / %s",
              s.streamlineFrameGeneration.requested ? "yes" : "no",
              s.streamlineFrameGeneration.backBufferTagged ? "tagged"
                                                           : "missing",
              s.streamlineFrameGeneration.tagsSubmitted ? "yes" : "no");
    MetricRow("Frame Gen Options / Tags / State", "%s / %s / %s",
              s.streamlineFrameGeneration.optionsConfigured ? "ready"
                                                            : "missing",
              s.streamlineFrameGeneration.tagsSubmitted ? "yes" : "no",
              s.streamlineFrameGeneration.stateValid ? "valid" : "none");
    MetricRow("Frame Gen Requested / Max / Presented", "%u / %u / %u",
              s.streamlineFrameGeneration.generatedFrames,
              s.streamlineFrameGeneration.maxGeneratedFrames,
              s.streamlineFrameGeneration.presentedFrames);
    MetricRow(
        "Frame Gen Status / VRAM MB", "0x%08X / %.2f",
        s.streamlineFrameGeneration.statusFlags,
        static_cast<double>(s.streamlineFrameGeneration.estimatedVramBytes) /
            (1024.0 * 1024.0));
    MetricRow("Frame Gen Runtime / Reason", "%s / %s",
              RENDER3D::UPSCALING::ToString(s.streamlineFrameGeneration.status),
              s.streamlineFrameGeneration.statusReason.empty()
                  ? "none"
                  : s.streamlineFrameGeneration.statusReason.c_str());
    MetricRow("Frame Gen Retry / Consecutive / Frame", "%s / %u / %llu",
              s.streamlineFrameGeneration.retryPending ? "cooldown" : "ready",
              s.streamlineFrameGeneration.consecutiveFailureCount,
              static_cast<unsigned long long>(
                  s.streamlineFrameGeneration.retryFrameIndex));
    MetricRow("Temporal Debug", "%s / %s",
              ToString(s.temporalResources.debugView),
              s.temporalResources.debugOutputReady ? "ready" : "missing");
    MetricRow("Invalidations / Resizes", "%llu / %llu",
              static_cast<unsigned long long>(
                  s.temporalResources.historyInvalidationCount),
              static_cast<unsigned long long>(
                  s.temporalResources.resourceResizeCount));
    ImGui::EndTable();
  }
}

void DrawReadiness(const RuntimePerformanceSnapshot &s) {
  ImGui::SeparatorText("Readiness");
  if (ImGui::BeginTable("ReadinessTable", 3,
                        ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextColumn();
    TextStatus("GPU Scene", s.mesh.surfaceGpuSceneSrvValid &&
                                s.mesh.surfaceGpuSceneBufferReady);
    TextStatus("Meshlet Args",
               s.mesh.meshletBackendDispatchArgumentBufferReady &&
                   s.mesh.meshletBackendDispatchCommandSignatureReady);
    ImGui::TableNextColumn();
    TextStatus("Cluster Cull", s.mesh.clusterGpuCullReady);
    TextStatus("Meshlet Draw",
               s.mesh.meshletBackendPipelineReady &&
                   s.mesh.meshletBackendDispatchArgumentBufferReady &&
                   s.mesh.meshletBackendDispatchCommandSignatureReady);
    ImGui::TableNextColumn();
    TextStatus("Cluster Resource",
               s.clusterResources.initialized &&
                   s.clusterResources.readyResourceCount > 0);
    TextStatus("Meshlet Backend", s.mesh.meshletBackendPipelineReady);
    TextStatus("GPU Timing", s.gpu.gpuTimingAvailable);
    ImGui::EndTable();
  }
}
#endif

} // namespace HIKARI::EDITOR::PERFORMANCE_AUDIT
