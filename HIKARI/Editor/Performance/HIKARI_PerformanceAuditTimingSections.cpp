#include "Core/Text/HIKARI_Localization.h"
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
void DrawFrameSummary(const RuntimePerformanceSnapshot& s) {
    const size_t mainlineSubmittedCount = s.mesh.meshletBackend.submittedDispatchCount > 0
                                              ? s.mesh.meshletBackend.submittedDispatchCount
                                              : s.mesh.clusterGpuCull.gpuDrawCommandCount;
    const bool clusterMainline = s.mesh.clusterGpuCull.submittedInstanceCount > 0 || mainlineSubmittedCount > 0;
    const bool gpuDrivenReady =
        s.mesh.gpuSceneUpload.bufferStats.srv.ptr != 0 && s.mesh.gpuSceneUpload.bufferStats.initialized;
    const double cpuFrameMs = GetCpuFrameMs(s.cpu);
    const double parentGpuMs = SumGpuMsByScope(s.gpu, false);
    const double nestedSubpassGpuMs = SumGpuMsByScope(s.gpu, true);

    if (ImGui::BeginTable("GpuDrivenFrameSummary", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::Text("FPS %.1f", s.fpsRaw);
        if (s.cpu.valid) {
            ImGui::Text(TEXT::Localize("editor.ui.cpu.frame.3f.ms.f03a1f").c_str(), cpuFrameMs);
            const CPU_PROFILE::PassTiming& frameTiming = s.cpu.passes[static_cast<size_t>(CPU_PROFILE::Pass::Frame)];
            ImGui::Text(TEXT::Localize("editor.ui.avg.3f.p95.3f.ms.220a6d").c_str(), frameTiming.averageMs,
                        frameTiming.p95Ms);
        } else {
            ImGui::TextDisabled(TEXT::Localize("editor.ui.cpu.timing.waiting.06aa72").c_str());
        }
        ImGui::Text(TEXT::Localize("editor.ui.fixed.u.dropped.2f.ms.b59587").c_str(), s.fixedStepsThisFrame,
                    static_cast<double>(s.droppedFixedTimeSeconds) * 1000.0);

        ImGui::TableNextColumn();
        ImGui::TextColored(StatusColor(gpuDrivenReady), TEXT::Localize("editor.ui.gpu.scene.s.920adb").c_str(),
                           ReadyText(gpuDrivenReady));
        ImGui::Text(TEXT::Localize("editor.ui.uploaded.zu.requested.zu.aa02c9").c_str(),
                    s.mesh.gpuSceneUpload.bufferStats.uploadedInstanceCount,
                    s.mesh.gpuSceneUpload.bufferStats.requestedInstanceCount);

        ImGui::TableNextColumn();
        ImGui::TextColored(StatusColor(clusterMainline), TEXT::Localize("editor.ui.cluster.s.9be94f").c_str(),
                           clusterMainline ? "Active" : "Idle");
        ImGui::Text(TEXT::Localize("editor.ui.dispatches.zu.draw.args.zu.a3f376").c_str(), mainlineSubmittedCount,
                    s.mesh.clusterGpuCull.gpuDrawCommandCount);

        ImGui::TableNextColumn();
        ImGui::Text(TEXT::Localize("editor.ui.gpu.parent.3f.ms.ea7ff2").c_str(), parentGpuMs);
        ImGui::Text(TEXT::Localize("editor.ui.nested.sub.3f.ms.bf7e52").c_str(), nestedSubpassGpuMs);
        ImGui::EndTable();
    }
}

void DrawCpuTimingTable(const RuntimePerformanceSnapshot& s) {
    ImGui::SeparatorText(TEXT::Localize("editor.ui.cpu.timing.cff982").c_str());
    if (!s.cpu.valid) {
        ImGui::TextDisabled(
            TEXT::Localize("editor.ui.cpu.timing.data.is.waiting.for.a.completed.frame.0848ab").c_str());
        return;
    }

    const double frameMs = (std::max)(0.0001, GetCpuFrameMs(s.cpu));
    ImGui::Text(TEXT::Localize("editor.ui.frame.llu.scopes.u.frame.3f.ms.ea6fdd").c_str(),
                static_cast<unsigned long long>(s.cpu.frameIndex), CountValidCpuPasses(s.cpu), frameMs);
    const CPU_PROFILE::PassTiming& frameTiming = s.cpu.passes[static_cast<size_t>(CPU_PROFILE::Pass::Frame)];
    ImGui::Text(TEXT::Localize("editor.ui.rolling.u.frames.avg.3f.p95.3f.p99.3f.max.3f.ms.fdf253").c_str(),
                frameTiming.rollingSampleCount, frameTiming.averageMs, frameTiming.p95Ms, frameTiming.p99Ms,
                frameTiming.maximumMs);
    ImGui::TextDisabled(TEXT::Localize("editor.ui.inclusive.timings.nested.scopes.are.not.additive.e24523").c_str());

    const auto drawTimingRow = [frameMs](const CPU_PROFILE::PassTiming& timing) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(timing.name);
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.3f", timing.cpuMs);
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.3f", timing.averageMs);
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%.3f", timing.p95Ms);
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%.3f", timing.p99Ms);
        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%.3f", timing.maximumMs);
        ImGui::TableSetColumnIndex(6);
        ImGui::Text("%u", timing.callCount);
        ImGui::TableSetColumnIndex(7);
        ImGui::ProgressBar(static_cast<float>((std::min)(1.0, timing.cpuMs / frameMs)), ImVec2(-1.0f, 0.0f), "");
    };

    if (ImGui::BeginTable("CpuFrameTimingTable", 8,
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Scope", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("P95", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("P99", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Frame Share");
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < s.cpu.passes.size(); ++i) {
            const CPU_PROFILE::PassTiming& timing = s.cpu.passes[i];
            if (!timing.valid && timing.maximumMs <= 0.0) {
                continue;
            }
            drawTimingRow(timing);
        }
        ImGui::EndTable();
    }

    std::array<const CPU_PROFILE::PassTiming*, CPU_PROFILE::kMaximumDynamicPasses> systemTimings{};
    uint32_t systemTimingCount = 0u;
    for (uint32_t index = 0u; index < s.cpu.dynamicPassCount; ++index) {
        const CPU_PROFILE::PassTiming& timing = s.cpu.dynamicPasses[index];
        if (timing.valid || timing.maximumMs > 0.0) {
            systemTimings[systemTimingCount++] = &timing;
        }
    }
    std::sort(systemTimings.begin(), systemTimings.begin() + systemTimingCount,
              [](const CPU_PROFILE::PassTiming* lhs, const CPU_PROFILE::PassTiming* rhs) {
                  if (lhs->p95Ms != rhs->p95Ms) {
                      return lhs->p95Ms > rhs->p95Ms;
                  }
                  return lhs->averageMs > rhs->averageMs;
              });

    ImGui::SeparatorText(TEXT::Localize("editor.ui.scene.system.cpu.e8ace7").c_str());
    ImGui::TextDisabled(
        TEXT::Localize("editor.ui.frame.scopes.aggregate.variable.updates.fixed.scopes.aggregate.a.93f7e5").c_str());
    if (systemTimingCount == 0u) {
        ImGui::TextDisabled(
            TEXT::Localize("editor.ui.system.timing.data.is.waiting.for.an.active.scene.768221").c_str());
    } else if (ImGui::BeginTable("CpuSystemTimingTable", 8,
                                 ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("System / Phase", ImGuiTableColumnFlags_WidthFixed, 250.0f);
        ImGui::TableSetupColumn("Now", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("P95", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("P99", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Frame Share");
        ImGui::TableHeadersRow();
        for (uint32_t index = 0u; index < systemTimingCount; ++index) {
            drawTimingRow(*systemTimings[index]);
        }
        ImGui::EndTable();
    }
}

void DrawGpuTimingTable(const RuntimePerformanceSnapshot& s) {
    ImGui::SeparatorText(TEXT::Localize("editor.ui.gpu.timing.10adc4").c_str());
    if (!s.gpu.gpuTimingAvailable) {
        ImGui::TextDisabled("%s", s.gpu.profilerEnabled ? "Timestamp query data is waiting." : s.gpu.unavailableReason);
        return;
    }

    ImGui::Text(TEXT::Localize("editor.ui.frame.llu.u.measured.passes.bcd895").c_str(),
                static_cast<unsigned long long>(s.gpu.frameIndex), CountValidGpuPasses(s.gpu));
    ImGui::TextDisabled(
        TEXT::Localize("editor.ui.sample.includes.child.passes.self.excludes.them.avg.p95.p99.last.c827ca").c_str());
    if (ImGui::BeginTable("GpuDrivenTimingTable", 8,
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (const char* title : {"Pass", "Parent", "Sample ms", "Self ms", "Avg ms", "P95 ms", "P99 ms", "Samples"})
            ImGui::TableSetupColumn(title);
        ImGui::TableHeadersRow();
        for (const auto& timing : s.gpu.passes) {
            if (!timing.valid)
                continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(timing.name);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(timing.parentPass == GFX::GPU_PROFILE::Pass::Count
                                       ? "-"
                                       : GFX::GPU_PROFILE::ToString(timing.parentPass));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", timing.gpuMs);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", timing.exclusiveGpuMs);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.3f", timing.averageMs);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%.3f", timing.p95Ms);
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%.3f", timing.p99Ms);
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%u", timing.rollingSampleCount);
        }
        ImGui::EndTable();
    }
}
#endif

} // namespace HIKARI::EDITOR::PERFORMANCE_AUDIT
