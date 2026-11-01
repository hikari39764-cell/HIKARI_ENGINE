#pragma once

#include "Editor/Performance/HIKARI_PerformanceAuditPanel.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"

#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_MetricRegistry.h"
#include "Editor/Performance/HIKARI_MetricText.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneJointPaletteUpload.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Lighting/HIKARI_VolumetricCloudStage.h"
#include "Render3D/Lighting/HIKARI_DynamicDiffuseGi.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Render3D/Material/HIKARI_GpuMaterialRegistry.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Render3D/ScreenSpace/HIKARI_SsaoRenderer.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Render3D/Shadow/HIKARI_LocalLightShadowRenderer.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"
#include "Render3D/Upscaling/HIKARI_StreamlineReflex.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace HIKARI::EDITOR::PERFORMANCE_AUDIT {

struct RuntimePerformanceSnapshot {
    // 性能指標はここから読む（CPU・GPU の計時、管線統計、FPS、VRAM、render.* の計数）。
    std::vector<METRIC::MetricReading> metrics{};
    uint32_t fixedStepsThisFrame = 0u;
    float droppedFixedTimeSeconds = 0.0f;
    int backBufferWidth = 0;
    int backBufferHeight = 0;
    std::string sceneId{};
    GFX::GfxDebugConfig debugConfig{};
    RENDER3D::RenderQualitySettings renderQuality{};
    RenderSubmissionDebugStats submission{};
    RENDER3D::RUNTIME::SceneRenderCache::Stats scene{};
    RENDER3D::GPUDRIVEN::GpuSceneRegistryStats gpuRegistry{};
    RENDER3D::GPUDRIVEN::GpuSceneJointPaletteUploadStats jointPaletteUpload{};
    RENDER3D::MATERIAL::GpuMaterialRegistryStats gpuMaterials{};
    MESHRENDERER::MeshRendererDebugStats mesh{};
    SHADOW::ShadowMapDebugStats shadow{};
    SHADOW::LocalLightShadowStats localShadow{};
    RENDER3D::DYNAMICGI::Stats dynamicGi{};
    RENDER3D::RAYTRACING::RuntimeRayTracingStats rayScene{};
    RENDER3D::ClusterGeometryResourceSystemStats clusterResources{};
    RENDER3D::RenderResourceDescriptorPoolStats descriptorPool{};
    RENDER3D::SCREENSPACE::SsaoDebugState ssao{};
    RENDER3D::SCREENSPACE::DepthVisibilityDebugState depthVisibility{};
    RENDER3D::VOLUMETRIC::VolumetricLightingStats volumetric{};
    RENDER3D::CLOUDS::VolumetricCloudStageStats clouds{};
    RENDER3D::TEMPORAL::TemporalFrameState temporalFrame{};
    RENDER3D::TEMPORAL::TemporalResourceStats temporalResources{};
    RENDER3D::UPSCALING::StreamlineDebugStats streamline{};
    RENDER3D::UPSCALING::StreamlineReflexStats streamlineReflex{};
    RENDER3D::UPSCALING::StreamlineFrameGenerationStats streamlineFrameGeneration{};
    POST::PresentationFrameResources presentation{};
};

const char* ReadyText(bool value);

double SafeDivide(uint64_t numerator, uint64_t denominator);

// gpu.<pass 名>.pipeline.<項> の指標を pass ごとに束ねた行。
struct PipelineStatsRow {
    std::string_view name{};
    const METRIC::MetricReading* calls = nullptr;
    const METRIC::MetricReading* asInvocations = nullptr;
    const METRIC::MetricReading* msInvocations = nullptr;
    const METRIC::MetricReading* msPrimitives = nullptr;
    const METRIC::MetricReading* clipPrimitives = nullptr;
    const METRIC::MetricReading* psInvocations = nullptr;
};
std::vector<PipelineStatsRow> PipelineStatsRows(const std::vector<METRIC::MetricReading>& metrics);

void AppendShadowCacheReason(std::string& text, uint32_t flags, uint32_t bit, const char* label);

std::string ShadowCacheMissReasonText(uint32_t flags);

ImVec4 StatusColor(bool ok);

void TextStatus(const char* label, bool ok);

void MetricRowText(const char* label, const char* value);

RuntimePerformanceSnapshot BuildSnapshot();
std::string BuildPerformanceReport(const RuntimePerformanceSnapshot& snapshot);

void DrawFrameSummary(const RuntimePerformanceSnapshot& s);

void DrawCpuTimingTable(const RuntimePerformanceSnapshot& s);

void DrawGpuTimingTable(const RuntimePerformanceSnapshot& s);

void DrawRenderPathTable(const RuntimePerformanceSnapshot& s);

void DrawGpuMaterialTable(const RuntimePerformanceSnapshot& s);

void DrawMeshShaderPipelineStatsTable(const RuntimePerformanceSnapshot& s);

void DrawGeometryPipelineTable(const RuntimePerformanceSnapshot& s);

void DrawResourceSummaryTable(const RuntimePerformanceSnapshot& s);

void DrawEffectsTable(const RuntimePerformanceSnapshot& s);

void DrawTemporalTable(const RuntimePerformanceSnapshot& s);

void DrawReadiness(const RuntimePerformanceSnapshot& s);

} // namespace HIKARI::EDITOR::PERFORMANCE_AUDIT
