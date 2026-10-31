#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Performance/HIKARI_PerformanceAuditPanel.h"

#include "Core/HIKARI_TimeService.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Render3D/Color/HIKARI_ExposureState.h"
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

#include "imgui.h"

#include "Editor/Performance/HIKARI_PerformanceAuditInternal.h"
#include <algorithm>
#include <cstdint>
#include <string>

namespace HIKARI::EDITOR::PERFORMANCE_AUDIT {

void DrawResourceSummaryTable(const RuntimePerformanceSnapshot& s) {
    ImGui::SeparatorText(TEXT::Localize("shell.resources").c_str());
    if (BeginMetricTable("ClusterResourceMetrics")) {
        MetricRow("Context / Ready / Failed", "%s / %u / %u", s.clusterResources.initialized ? "Ready" : "Missing",
                  s.clusterResources.readyResourceCount, s.clusterResources.failedCount);
        MetricRow("Requests / Hits / Misses / Loaded", "%u / %u / %u / %u", s.clusterResources.requestCount,
                  s.clusterResources.hitCount, s.clusterResources.missCount, s.clusterResources.loadedCount);
        MetricRow("Surfaces / LOD Ranges / Clusters / Pages / Ranges", "%u / %u / %u / %u / %u",
                  s.clusterResources.surfaceCount, s.clusterResources.surfaceLodRangeCount,
                  s.clusterResources.clusterCount, s.clusterResources.pageCount, s.clusterResources.surfaceRangeCount);
        MetricRow("Vertices / Indices / Primitives / GPU Bytes", "%u / %u / %u / %.2f MB",
                  s.clusterResources.vertexCount, s.clusterResources.indexCount,
                  s.clusterResources.meshletPrimitiveCount,
                  static_cast<double>(s.clusterResources.gpuBufferBytes) / (1024.0 * 1024.0));
        MetricRow("Shader SRV / Missing / Allocation Failed", "%u / %u / %u",
                  s.clusterResources.shaderVisibleResourceCount, s.clusterResources.missingDescriptorCount,
                  s.clusterResources.descriptorAllocationFailedCount);
        MetricRow("File Reads / GPU Creates / Uploads / Fence Waits", "%u / %u / %u / %u",
                  s.clusterResources.fileReadCount, s.clusterResources.gpuResourceCreateCount,
                  s.clusterResources.uploadSubmitCount, s.clusterResources.fenceWaitCount);
        MetricRow("Descriptors Allocated / Owner Violations", "%u / %u", s.clusterResources.descriptorAllocationCount,
                  s.clusterResources.ownerThreadViolationCount);
        MetricRow("Descriptor Pool Used / Capacity / Failed", "%u / %u / %u", s.descriptorPool.used,
                  s.descriptorPool.capacity, s.descriptorPool.failedAllocationCount);
        MetricRow("Cluster Descriptor Used / Capacity / Failed", "%u / %u / %u", s.descriptorPool.clusterUsed,
                  s.descriptorPool.clusterCapacity, s.descriptorPool.clusterFailedAllocationCount);
        ImGui::EndTable();
    }
}

void DrawEffectsTable(const RuntimePerformanceSnapshot& s) {
    ImGui::SeparatorText(TEXT::Localize("ui.vfx.effects").c_str());
    if (BeginMetricTable("EffectsMetrics", 250.0f)) {
        MetricRow("SSAO Mode / Valid / Half", "%s / %s / %s", GetSsaoModeName(s.ssao.mode), s.ssao.valid ? "yes" : "no",
                  s.ssao.halfResolution ? "yes" : "no");
        MetricRow("SSAO AO Size / Samples / Blur", "%u x %u / %u / %u", s.ssao.internalWidth, s.ssao.internalHeight,
                  s.ssao.sampleCount, s.ssao.blurIterations);
        MetricRow("SSAO CPU Main / Blur / Total", "%.3f / %.3f / %.3f ms", s.ssao.mainCpuMs, s.ssao.blurCpuMs,
                  s.ssao.totalCpuMs);
        MetricRow("Cloud Trace / Solar Visibility", "%s / %s", s.clouds.rendered ? "ready" : "off",
                  s.clouds.solarVisibilityReady ? "ready" : "unavailable");
        MetricRow("Cloud Trace / Solar MiB", "%.2f / %.2f", double(s.clouds.workingSetBytes) / (1024 * 1024),
                  double(s.clouds.solarBytes) / (1024 * 1024));
        MetricRow("Cloud Solar Columns / Steps", "%u x %u / %u", s.clouds.solarResolution, s.clouds.solarResolution,
                  s.clouds.solarSteps);
        MetricRow("Volumetric Requested / Active / History", "%s / %s / %s", s.volumetric.requested ? "yes" : "no",
                  s.volumetric.active ? "yes" : "no", s.volumetric.historyValid ? "valid" : "cold");
        MetricRow("Volumetric Quality / Froxels / Tile", "%s / %u x %u x %u / %u px",
                  RENDER3D::VolumetricLightingQualityLabel(
                      static_cast<RENDER3D::VolumetricLightingQuality>(s.volumetric.qualityLevel)),
                  s.volumetric.froxelWidth, s.volumetric.froxelHeight, s.volumetric.froxelDepth,
                  s.volumetric.froxelPixelSize);
        MetricRow("Volumetric Working Set", "%.2f MB",
                  static_cast<double>(s.volumetric.workingSetBytes) / (1024.0 * 1024.0));
        MetricRow("Volumetric Shadow / Local (P/S) / History", "%s / %u (%u/%u) / %.2f",
                  s.volumetric.shadowed ? "yes" : "no", s.volumetric.localLightCount, s.volumetric.pointLightCount,
                  s.volumetric.spotLightCount, s.volumetric.temporalWeight);
        MetricRow("Volumetric Local Shadow Faces", "%u", s.volumetric.localShadowFaces);
        MetricRow("Volumetric Dispatch / Resizes / Resets", "%u / %llu / %llu", s.volumetric.dispatchCount,
                  static_cast<unsigned long long>(s.volumetric.resizeCount),
                  static_cast<unsigned long long>(s.volumetric.historyResetCount));
        MetricRow("Local Shadows Selected / Requested / Budget Rejected", "%u / %u / %u", s.localShadow.selectedLights,
                  s.localShadow.requestedLights, s.localShadow.budgetRejectedLights);
        MetricRow("Local Shadow Faces Drawn / Reused / Failed Lights", "%u / %u / %u", s.localShadow.renderedFaces,
                  s.localShadow.reusedFaces, s.localShadow.failedLights);
        MetricRow("Local Shadow Depth MiB", "%.2f", double(s.localShadow.allocatedDepthBytes) / (1024.0 * 1024.0));
        MetricRow("Dynamic GI", "%s", RENDER3D::DYNAMICGI::ToString(s.dynamicGi.status));
        MetricRow("GI Updated / Resident Probes / Primary Rays", "%u / %u / %u", s.dynamicGi.updatedProbes,
                  s.dynamicGi.probes, s.dynamicGi.primaryRays);
        MetricRow("GI Probe / RT Resident / RT Retiring MiB", "%.2f / %.2f / %.2f",
                  double(s.dynamicGi.probeBytes) / (1024 * 1024), double(s.rayScene.residentBytes) / (1024 * 1024),
                  double(s.rayScene.retiringBytes) / (1024 * 1024));
        uint32_t excludedSurfaces = 0;
        for (const uint32_t count : s.rayScene.excluded.surfaces) {
            excludedSurfaces += count;
        }
        MetricRow("Shared RT Pending / Excluded / Missing", "%u / %u / %u", s.rayScene.pendingChunks, excludedSurfaces,
                  s.rayScene.excluded
                      .surfaces[static_cast<size_t>(RENDER3D::RAYTRACING::RuntimeRayTracingExclusion::Missing)]);
        MetricRow("GI Local Lights / Overflow", "%u / %u", s.dynamicGi.localLights, s.dynamicGi.droppedLights);
        MetricRow("Probe Sampling / Dynamic GI Quality", "%s / %s",
                  RENDER3D::LightProbeVolumeSamplingModeLabel(s.renderQuality.lightProbeVolumeSampling),
                  RENDER3D::DynamicGiQualityLabel(s.renderQuality.dynamicGiQuality));
        MetricRow("Shadow Enabled / Resolution / Ortho / Texel", "%s / %u / %.2f / %.5f",
                  s.shadow.enabled ? "yes" : "no", s.shadow.resolution, s.shadow.orthoSize, s.shadow.worldTexelSize);
        MetricRow("Shadow PCF / Radius / Bias / Normal Bias / Strength", "%s / %.2f / %.4f / %.4f / %.2f",
                  s.shadow.pcfEnabled ? "on" : "off", s.shadow.pcfRadius, s.shadow.depthBias, s.shadow.normalBias,
                  s.shadow.strength);
        const std::string currentMissReason = ShadowCacheMissReasonText(s.shadow.shadowCacheMissReasonFlags);
        const std::string lastMissReason = ShadowCacheMissReasonText(s.shadow.shadowCacheLastMissReasonFlags);
        MetricRow("Shadow Depth Cache Valid / Hit / Miss / Last @ Logical Frame", "%s / %s / %s / %s @ %llu",
                  s.shadow.shadowCacheValid ? "yes" : "no", s.shadow.shadowCacheHit ? "yes" : "no",
                  currentMissReason.c_str(), lastMissReason.c_str(),
                  static_cast<unsigned long long>(s.shadow.shadowCacheLastMissFrameIndex));
        MetricRow("Shadow Depth Cache Hits / Misses / Copy / Update", "%zu / %zu / %zu / %zu",
                  s.shadow.shadowCacheHitCount, s.shadow.shadowCacheMissCount, s.shadow.shadowStaticCacheCopyCount,
                  s.shadow.shadowStaticCacheUpdateCount);
        MetricRow("Shadow Source Cache Full / Patch / Reuse / Updated", "%zu / %zu / %zu / %zu",
                  s.shadow.shadowSourceFullSyncCount, s.shadow.shadowSourcePatchCount, s.shadow.shadowSourceReuseCount,
                  s.shadow.shadowSourceUpdatedInstanceCount);
        MetricRow("Shadow Sources Static / Dynamic / Drawn", "%zu / %zu / %s %s %s",
                  s.shadow.shadowStaticSourceInstanceCount, s.shadow.shadowDynamicSourceInstanceCount,
                  s.shadow.shadowStaticRendered ? "static" : "-", s.shadow.shadowDynamicRendered ? "dynamic" : "-",
                  s.shadow.shadowFallbackRendered ? "fallback" : "-");
        MetricRow("Shadow Meshlet Dispatch Req / Submitted / Skip", "%zu / %zu / %zu",
                  s.shadow.shadowMeshletRequestedDispatchCount, s.shadow.shadowMeshletSubmittedDispatchCount,
                  s.shadow.shadowMeshletSkippedDispatchCount);
        ImGui::EndTable();
    }
}

void DrawTemporalTable(const RuntimePerformanceSnapshot& s) {
    ImGui::SeparatorText(TEXT::Localize("ui.debugView.temporal").c_str());
    if (BeginMetricTable("TemporalMetrics", 250.0f)) {
        const RENDER3D::RenderQualitySettings& renderQuality = s.renderQuality;
        MetricRowText("Scene", s.sceneId.empty() ? "unavailable" : s.sceneId.c_str());
        MetricRow("Camera Valid / Source / Position", "%s / %llu / %.2f, %.2f, %.2f",
                  s.temporalFrame.camera.valid ? "yes" : "no",
                  static_cast<unsigned long long>(s.temporalFrame.cameraSourceObjectId),
                  s.temporalFrame.camera.cameraPos.x, s.temporalFrame.camera.cameraPos.y,
                  s.temporalFrame.camera.cameraPos.z);
        MetricRow("Camera FOV Y / Near / Far / Aspect", "%.3f / %.3f / %.3f / %.3f", s.temporalFrame.camera.fovYRad,
                  s.temporalFrame.camera.nearZ, s.temporalFrame.camera.farZ, s.temporalFrame.camera.aspect);
        MetricRow("Scene Resolution / Window Size / Mode", "%s / %s / %s",
                  RENDER3D::RenderResolutionPresetLabel(renderQuality.sceneResolution),
                  RENDER3D::RenderResolutionPresetLabel(renderQuality.windowSize),
                  RENDER3D::WindowPresentationModeLabel(renderQuality.windowMode));
        MetricRow("Viewport Scale / VSync / Scene Depth Prepass", "%.2f / %s / %s", renderQuality.viewportScale,
                  renderQuality.vSync ? "on" : "off", renderQuality.sceneDepthPrepass ? "on" : "off");
        MetricRow("Geometry Pipeline / Forward Cost", "%s / %s",
                  RENDER3D::GeometryPipelineModeLabel(renderQuality.geometryPipeline),
                  RENDER3D::ForwardShadingCostModeLabel(renderQuality.forwardCostMode));
        MetricRow("AA Mode / DLSS Quality", "%s / %s",
                  RENDER3D::RenderAntiAliasingModeLabel(renderQuality.antiAliasingMode),
                  RENDER3D::DlssQualityModeLabel(renderQuality.dlssQualityMode));
        MetricRow("Frame Generation Policy / Multiplier", "%s / %ux",
                  RENDER3D::RenderFrameGenerationModeLabel(renderQuality.frameGenerationMode),
                  static_cast<unsigned int>(renderQuality.frameGenerationMultiplier));
        MetricRow("Probe Sampling / GI / Local Shadow", "%s / %s / %s",
                  RENDER3D::LightProbeVolumeSamplingModeLabel(renderQuality.lightProbeVolumeSampling),
                  RENDER3D::DynamicGiQualityLabel(renderQuality.dynamicGiQuality),
                  RENDER3D::LocalShadowQualityLabel(renderQuality.localShadowQuality));
        MetricRow("Volumetric / Stylized Cloud Quality", "%s / %s",
                  RENDER3D::VolumetricLightingQualityLabel(renderQuality.volumetricLightingQuality),
                  RENDER3D::VolumetricCloudQualityLabel(renderQuality.volumetricCloudQuality));
        MetricRow("TAA History / Variance / Depth / Luma / Sharpness", "%.3f / %.3f / %.4f / %.3f / %.3f",
                  renderQuality.taaHistoryWeight, renderQuality.taaVarianceClipGamma, renderQuality.taaDepthRejection,
                  renderQuality.taaLuminanceRejection, renderQuality.taaSharpness);
        MetricRow(
            "Frame / History / Reset", "%llu / %s / %s", static_cast<unsigned long long>(s.temporalFrame.frameIndex),
            s.temporalFrame.historyValid ? "valid" : "cold", RENDER3D::TEMPORAL::ToString(s.temporalFrame.resetReason));
        MetricRow("Render / Output Size", "%u x %u / %u x %u", s.temporalFrame.renderWidth,
                  s.temporalFrame.renderHeight, s.temporalFrame.outputWidth, s.temporalFrame.outputHeight);
        MetricRow("Presentation H / UI / Final", "%s / %s / %s", s.presentation.hudlessReady ? "ready" : "missing",
                  s.presentation.uiReady ? "ready" : "missing", s.presentation.finalReady ? "ready" : "missing");
        MetricRow("Presentation Size / FG Inputs", "%d x %d / %s", s.presentation.width, s.presentation.height,
                  s.presentation.HasFrameGenerationInputs() ? "ready" : "missing");
        MetricRow("Presentation UI Logical", "%d x %d", s.presentation.uiLogicalWidth, s.presentation.uiLogicalHeight);
        MetricRow("Jitter / Phase / Pixels", "%s / %u / %.3f, %.3f", s.temporalFrame.jitterEnabled ? "on" : "off",
                  s.temporalFrame.jitterPhase, s.temporalFrame.camera.jitter.x, s.temporalFrame.camera.jitter.y);
        MetricRow("Motion RG / Metadata / Written", "%s / %s / %s",
                  s.temporalResources.motionVectorReady ? "yes" : "no",
                  s.temporalResources.motionMetadataReady ? "yes" : "no",
                  s.temporalResources.motionVectorWritten ? "yes" : "no");
        MetricRow("Velocity Draws Rigid / Skinned / AlphaMask", "%u / %u / %u",
                  s.temporalResources.rigidVelocityDrawCount, s.temporalResources.skinnedVelocityDrawCount,
                  s.temporalResources.alphaMaskedVelocityDrawCount);
        MetricRow("Scene Color / TAA", "%s / %s / %s", s.temporalResources.sceneColorReady ? "yes" : "no",
                  s.temporalResources.taaEnabled ? "on" : "off",
                  s.temporalResources.taaResolved ? "resolved" : "skipped");
        MetricRow("History Color / Depth / Valid", "%s / %s / %s", s.temporalResources.historyColorReady ? "yes" : "no",
                  s.temporalResources.historyDepthReady ? "yes" : "no",
                  (s.temporalResources.historyColorValid && s.temporalResources.historyDepthValid) ? "yes" : "no");
        MetricRow("TAA Resolved Ready", "%s", s.temporalResources.taaResolvedColorReady ? "yes" : "no");
        MetricRow("Exposure Ready / Written", "%s / %s", s.temporalResources.exposureReady ? "yes" : "no",
                  s.temporalResources.exposureWritten ? "yes" : "no");
        MetricRow("Exposure Mode / Multiplier / Pre", "%s / %.3f / %.3f",
                  TEXT::EnumToName(RENDER3D::COLOR::kExposureModeNames, s.temporalResources.exposureMode),
                  s.temporalResources.exposure, s.temporalResources.preExposure);
        MetricRow("Masks R/T/Invalid Ready / Valid / Source", "%s / %s / %s",
                  (s.temporalResources.reactiveMaskReady && s.temporalResources.transparencyMaskReady &&
                   s.temporalResources.invalidDepthMotionMaskReady)
                      ? "yes"
                      : "no",
                  s.temporalResources.masksWritten ? "yes" : "no",
                  s.temporalResources.masksCompositionDerived ? "composition-diff" : "opaque-zero");
        MetricRow("Streamline SDK / Init / Device", "%s / %s / %s", s.streamline.sdkCompiled ? "yes" : "no",
                  s.streamline.initialized ? "yes" : "no", s.streamline.deviceAttached ? "yes" : "no");
        MetricRow(
            "SL Plugins DLSS / FG / Reflex / PCL", "%s / %s / %s / %s", s.streamline.dlssPluginPresent ? "yes" : "no",
            s.streamlineFrameGeneration.pluginPresent ? "yes" : "no",
            s.streamlineReflex.reflexPluginPresent ? "yes" : "no", s.streamlineReflex.pclPluginPresent ? "yes" : "no");
        MetricRow("DLSS-G Loaded / Supported", "%s / %s", s.streamlineFrameGeneration.featureLoaded ? "yes" : "no",
                  s.streamlineFrameGeneration.supported ? "yes" : "no");
        MetricRow("DLSS Support / Token / Constants", "%s / %s / %s", s.streamline.dlssSupported ? "yes" : "no",
                  s.streamline.frameTokenReady ? "yes" : "no", s.streamline.constantsSubmitted ? "yes" : "no");
        MetricRow("DLSS Mode / Requested / Evaluated", "%s / %s / %s", RENDER3D::UPSCALING::ToString(s.streamline.mode),
                  s.streamline.dlssRequested ? "yes" : "no", s.streamline.dlssEvaluated ? "yes" : "no");
        MetricRow("DLSS Fallback / Output / Evaluations", "%s / %s / %llu", s.streamline.fallbackUsed ? "yes" : "no",
                  s.streamline.outputReady ? "ready" : "missing",
                  static_cast<unsigned long long>(s.streamline.evaluationCount));
        MetricRow("DLSS Failures / VRAM MB", "%llu / %.2f", static_cast<unsigned long long>(s.streamline.failureCount),
                  static_cast<double>(s.streamline.estimatedVramBytes) / (1024.0 * 1024.0));
        MetricRow("DLSS Render / Output", "%u x %u / %u x %u", s.streamline.renderWidth, s.streamline.renderHeight,
                  s.streamline.outputWidth, s.streamline.outputHeight);
        MetricRow("DLSS Optimal / Min / Max", "%u x %u / %u x %u / %u x %u", s.streamline.optimalRenderWidth,
                  s.streamline.optimalRenderHeight, s.streamline.minRenderWidth, s.streamline.minRenderHeight,
                  s.streamline.maxRenderWidth, s.streamline.maxRenderHeight);
        MetricRow("Streamline Status / Last", "%s / %s: %s", RENDER3D::UPSCALING::ToString(s.streamline.status),
                  s.streamline.lastOperation.empty() ? "none" : s.streamline.lastOperation.c_str(),
                  s.streamline.lastResult.empty() ? "none" : s.streamline.lastResult.c_str());
        MetricRow("DLSS Retry / Consecutive / Frame", "%s / %u / %llu",
                  s.streamline.retryPending ? "cooldown" : "ready", s.streamline.consecutiveFailureCount,
                  static_cast<unsigned long long>(s.streamline.retryFrameIndex));
        MetricRow("Reflex Support / Options / Sleep", "%s / %s / %s", s.streamlineReflex.reflexSupported ? "yes" : "no",
                  s.streamlineReflex.optionsConfigured ? "ready" : "missing",
                  s.streamlineReflex.sleepCalled ? "yes" : "no");
        MetricRow("Reflex PCL / Markers / Failures", "%s / 0x%02X / %llu",
                  s.streamlineReflex.pclSupported ? "yes" : "no", s.streamlineReflex.markerMask,
                  static_cast<unsigned long long>(s.streamlineReflex.failureCount));
        MetricRow("Frame Gen Support / Host / Inputs", "%s / %s / %s",
                  s.streamlineFrameGeneration.supported ? "yes" : "no",
                  s.streamlineFrameGeneration.hostAllowed ? "game" : "editor-off",
                  s.streamlineFrameGeneration.inputsReady ? "ready" : "missing");
        MetricRow("Frame Gen Requested / BackBuffer / Inputs", "%s / %s / %s",
                  s.streamlineFrameGeneration.requested ? "yes" : "no",
                  s.streamlineFrameGeneration.backBufferTagged ? "tagged" : "missing",
                  s.streamlineFrameGeneration.tagsSubmitted ? "yes" : "no");
        MetricRow("Frame Gen Options / Tags / State", "%s / %s / %s",
                  s.streamlineFrameGeneration.optionsConfigured ? "ready" : "missing",
                  s.streamlineFrameGeneration.tagsSubmitted ? "yes" : "no",
                  s.streamlineFrameGeneration.stateValid ? "valid" : "none");
        MetricRow("Frame Gen Requested / Max / Presented", "%u / %u / %u", s.streamlineFrameGeneration.generatedFrames,
                  s.streamlineFrameGeneration.maxGeneratedFrames, s.streamlineFrameGeneration.presentedFrames);
        MetricRow("Frame Gen Status / VRAM MB", "0x%08X / %.2f", s.streamlineFrameGeneration.statusFlags,
                  static_cast<double>(s.streamlineFrameGeneration.estimatedVramBytes) / (1024.0 * 1024.0));
        MetricRow("Frame Gen Runtime / Reason", "%s / %s",
                  RENDER3D::UPSCALING::ToString(s.streamlineFrameGeneration.status),
                  s.streamlineFrameGeneration.statusReason.empty() ? "none"
                                                                   : s.streamlineFrameGeneration.statusReason.c_str());
        MetricRow("Frame Gen Retry / Consecutive / Frame", "%s / %u / %llu",
                  s.streamlineFrameGeneration.retryPending ? "cooldown" : "ready",
                  s.streamlineFrameGeneration.consecutiveFailureCount,
                  static_cast<unsigned long long>(s.streamlineFrameGeneration.retryFrameIndex));
        MetricRow("Temporal Debug", "%s / %s", ToString(s.temporalResources.debugView),
                  s.temporalResources.debugOutputReady ? "ready" : "missing");
        MetricRow("Invalidations / Resizes", "%llu / %llu",
                  static_cast<unsigned long long>(s.temporalResources.historyInvalidationCount),
                  static_cast<unsigned long long>(s.temporalResources.resourceResizeCount));
        ImGui::EndTable();
    }
}

void DrawReadiness(const RuntimePerformanceSnapshot& s) {
    ImGui::SeparatorText(TEXT::Localize("editor.ui.readiness.6dc122").c_str());
    if (ImGui::BeginTable("ReadinessTable", 3, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        TextStatus("GPU Scene",
                   s.mesh.gpuSceneUpload.bufferStats.srv.ptr != 0 && s.mesh.gpuSceneUpload.bufferStats.initialized);
        TextStatus("Meshlet Args",
                   s.mesh.meshletDispatchArgumentBufferReady && s.mesh.meshletDispatchCommandSignatureReady);
        ImGui::TableNextColumn();
        TextStatus("Cluster Cull", s.mesh.clusterGpuCull.IsReady());
        TextStatus("Meshlet Draw", s.mesh.meshletBackend.pipelineReady && s.mesh.meshletDispatchArgumentBufferReady &&
                                       s.mesh.meshletDispatchCommandSignatureReady);
        ImGui::TableNextColumn();
        TextStatus("Cluster Resource", s.clusterResources.initialized && s.clusterResources.readyResourceCount > 0);
        TextStatus("Meshlet Backend", s.mesh.meshletBackend.pipelineReady);
        TextStatus("GPU Timing", IsFresh(METRIC::FindReading(s.metrics, METRIC::kFrameGpuMs)));
        ImGui::EndTable();
    }
}

} // namespace HIKARI::EDITOR::PERFORMANCE_AUDIT
