#pragma once

#include "Editor/Performance/HIKARI_PerformanceAuditPanel.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"

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

#include <algorithm>
#include <cstdint>
#include <string>

namespace HIKARI::EDITOR::PERFORMANCE_AUDIT {

#if defined(HIKARI_WITH_EDITOR)
struct RuntimePerformanceSnapshot {
  float fpsRaw = 0.0f;
  RenderSubmissionDebugStats submission{};
  RENDER3D::RUNTIME::SceneRenderCache::Stats scene{};
  RENDER3D::GPUDRIVEN::GpuSceneRegistryStats gpuRegistry{};
  RENDER3D::MATERIAL::GpuMaterialRegistryStats gpuMaterials{};
  MESHRENDERER::MeshRendererDebugStats mesh{};
  SHADOW::ShadowMapDebugStats shadow{};
  RENDER3D::ClusterGeometryResourceSystemStats clusterResources{};
  RENDER3D::RenderResourceDescriptorPoolStats descriptorPool{};
  RENDER3D::SCREENSPACE::SsaoDebugState ssao{};
  RENDER3D::SCREENSPACE::DepthVisibilityDebugState depthVisibility{};
  RENDER3D::VOLUMETRIC::VolumetricLightingStats volumetric{};
  RENDER3D::TEMPORAL::TemporalFrameState temporalFrame{};
  RENDER3D::TEMPORAL::TemporalResourceStats temporalResources{};
  RENDER3D::UPSCALING::StreamlineDebugStats streamline{};
  RENDER3D::UPSCALING::StreamlineReflexStats streamlineReflex{};
  RENDER3D::UPSCALING::StreamlineFrameGenerationStats
      streamlineFrameGeneration{};
  POST::PresentationFrameResources presentation{};
  CPU_PROFILE::FrameSnapshot cpu{};
  GFX::GPU_PROFILE::FrameSnapshot gpu{};
  GFX::GPU_PIPELINE_STATS::FrameSnapshot pipelineStats{};
};

bool IsClusterSubpass(GFX::GPU_PROFILE::Pass pass);

bool IsShadowSubpass(GFX::GPU_PROFILE::Pass pass);

bool IsNestedGpuSubpass(GFX::GPU_PROFILE::Pass pass);

const char *TimingScopeText(GFX::GPU_PROFILE::Pass pass);

const char *ReadyText(bool value);

double SafeDivide(uint64_t numerator, uint64_t denominator);

bool IsMeshletPipelineStatsPass(GFX::GPU_PROFILE::Pass pass);

void AppendShadowCacheReason(std::string &text, uint32_t flags, uint32_t bit,
                             const char *label);

std::string ShadowCacheMissReasonText(uint32_t flags);

ImVec4 StatusColor(bool ok);

void TextStatus(const char *label, bool ok);

void MetricRowText(const char *label, const char *value);

RuntimePerformanceSnapshot BuildSnapshot();

double SumGpuMsByScope(const GFX::GPU_PROFILE::FrameSnapshot &profile,
                       bool nestedSubpass);

uint32_t CountValidGpuPasses(const GFX::GPU_PROFILE::FrameSnapshot &profile);

double GetCpuFrameMs(const CPU_PROFILE::FrameSnapshot &profile);

uint32_t CountValidCpuPasses(const CPU_PROFILE::FrameSnapshot &profile);

template <typename... Args>
void MetricRow(const char *label, const char *format, Args... args) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::TextUnformatted(label);
  ImGui::TableSetColumnIndex(1);
  ImGui::Text(format, args...);
}

void DrawFrameSummary(const RuntimePerformanceSnapshot &s);

void DrawCpuTimingTable(const RuntimePerformanceSnapshot &s);

void DrawGpuTimingTable(const RuntimePerformanceSnapshot &s);

void DrawRenderPathTable(const RuntimePerformanceSnapshot &s);

void DrawGpuMaterialTable(const RuntimePerformanceSnapshot &s);

void DrawMeshShaderPipelineStatsTable(const RuntimePerformanceSnapshot &s);

void DrawGeometryPipelineTable(const RuntimePerformanceSnapshot &s);

void DrawResourceSummaryTable(const RuntimePerformanceSnapshot &s);

void DrawEffectsTable(const RuntimePerformanceSnapshot &s);

void DrawTemporalTable(const RuntimePerformanceSnapshot &s);

void DrawReadiness(const RuntimePerformanceSnapshot &s);
#endif

} // namespace HIKARI::EDITOR::PERFORMANCE_AUDIT
