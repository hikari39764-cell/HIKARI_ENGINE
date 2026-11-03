#include "Core/Text/HIKARI_Localization.h"
#include "HIKARI_PerformanceAuditPanel.h"
#include "Editor/Shell/HIKARI_EditorDockLayout.h"

#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_GpuPipelineStatsProfiler.h"
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

namespace HIKARI {

using namespace EDITOR::PERFORMANCE_AUDIT;

void PerformanceAuditPanel::Draw(bool& open, bool revealRequested) const {
    if (!open) {
        return;
    }
    const std::string windowName = TEXT::LocalizeWindow("editor.ui.performance.audit.e54bcf", "Performance Audit");
    // 開く操作と工作区の切替時だけ、旧配置の非表示ドックから復帰させる。
    if (revealRequested) {
        EDITOR::RevealEditorWindow(windowName.c_str());
    }
    if (!ImGui::Begin(windowName.c_str(), &open)) {
        ImGui::End();
        return;
    }

    DrawContents();

    ImGui::End();
}

void PerformanceAuditPanel::DrawContents() const {
    const bool copyRequested =
        ImGui::Button(TEXT::LocalizeWindow("editor.copyDiagnostics", "診断結果をコピー").c_str());
    ImGui::SameLine();
    const bool restartRequested =
        ImGui::Button(TEXT::LocalizeWindow("editor.performance.restartStatistics", "重新开始统计").c_str());
    GFX::GfxDebugConfig debugConfig = GFX::GetGfxDebugConfig();
    const bool pipelineStatsChanged =
        ImGui::Checkbox(TEXT::LocalizeWindow("editor.performance.pipelineStats", "GPU 管线统计（按需开启）").c_str(),
                        &debugConfig.enableGpuPipelineStats);
    if (pipelineStatsChanged) {
        GFX::SetGfxDebugConfig(debugConfig);
    }
    if (restartRequested || pipelineStatsChanged) {
        CPU_PROFILE::ResetStatistics();
        GFX::GPU_PROFILE::ResetStatistics();
        GFX::GPU_PIPELINE_STATS::ResetStatistics();
    }
    ImGui::TextDisabled(TEXT::Localize("editor.captureStatsHint").c_str());
    ImGui::TextDisabled(TEXT::Localize("editor.performance.pipelineStatsHint").c_str());
    const RuntimePerformanceSnapshot snapshot = BuildSnapshot();
    if (copyRequested) {
        const auto report = BuildPerformanceReport(snapshot);
        ImGui::SetClipboardText(report.c_str());
    }
    DrawFrameSummary(snapshot);
    DrawReadiness(snapshot);
    DrawCpuTimingTable(snapshot);
    DrawGpuTimingTable(snapshot);
    DrawMeshShaderPipelineStatsTable(snapshot);
    DrawRenderPathTable(snapshot);
    DrawGpuMaterialTable(snapshot);
    DrawUploadTrafficTable(snapshot);
    DrawGeometryPipelineTable(snapshot);
    DrawTemporalTable(snapshot);
    DrawEffectsTable(snapshot);
    DrawResourceSummaryTable(snapshot);
}

} // namespace HIKARI
