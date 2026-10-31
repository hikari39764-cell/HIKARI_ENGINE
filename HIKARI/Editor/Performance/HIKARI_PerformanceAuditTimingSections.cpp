#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Performance/HIKARI_PerformanceAuditPanel.h"

#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_DXCheck.h"
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

#include "imgui.h"

#include "Editor/Performance/HIKARI_PerformanceAuditInternal.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace HIKARI::EDITOR::PERFORMANCE_AUDIT {

namespace {
// フレームの区間（CPU_PROFILE::Pass）なら列挙の番号、そうでなければ -1。後者はシーンのシステムごとの計時。
int FramePhaseOrder(std::string_view name) {
    for (uint32_t pass = 1; pass < static_cast<uint32_t>(CPU_PROFILE::Pass::Count); ++pass) {
        if (name == CPU_PROFILE::ToString(static_cast<CPU_PROFILE::Pass>(pass))) {
            return static_cast<int>(pass);
        }
    }
    return -1;
}
} // namespace

void DrawFrameSummary(const RuntimePerformanceSnapshot& s) {
    const size_t mainlineSubmittedCount = s.mesh.meshletBackend.submittedDispatchCount > 0
                                              ? s.mesh.meshletBackend.submittedDispatchCount
                                              : s.mesh.clusterGpuCull.gpuDrawCommandCount;
    const bool clusterMainline = s.mesh.clusterGpuCull.submittedInstanceCount > 0 || mainlineSubmittedCount > 0;
    const bool gpuDrivenReady =
        s.mesh.gpuSceneUpload.bufferStats.srv.ptr != 0 && s.mesh.gpuSceneUpload.bufferStats.initialized;
    const METRIC::MetricReading* fps = METRIC::FindReading(s.metrics, METRIC::kFrameFps);
    const METRIC::MetricReading* cpuFrame = METRIC::FindReading(s.metrics, METRIC::kFrameCpuMs);
    const METRIC::MetricReading* gpuFrame = METRIC::FindReading(s.metrics, METRIC::kFrameGpuMs);

    if (ImGui::BeginTable("GpuDrivenFrameSummary", 4, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::Text("FPS %s", MetricText(fps, 1).c_str());
        if (IsFresh(cpuFrame)) {
            ImGui::Text(TEXT::Localize("editor.ui.cpu.frame.3f.ms.f03a1f").c_str(), cpuFrame->value);
            ImGui::Text(TEXT::Localize("editor.ui.avg.3f.p95.3f.ms.220a6d").c_str(), cpuFrame->window.average,
                        cpuFrame->window.p95);
        } else {
            ImGui::TextDisabled(TEXT::Localize("editor.performance.cpuFrameState").c_str(),
                                MetricText(cpuFrame, 3).c_str());
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
        if (IsFresh(gpuFrame)) {
            ImGui::Text(TEXT::Localize("editor.performance.gpuFrame").c_str(), gpuFrame->value);
            ImGui::Text(TEXT::Localize("editor.ui.avg.3f.p95.3f.ms.220a6d").c_str(), gpuFrame->window.average,
                        gpuFrame->window.p95);
        } else {
            ImGui::TextDisabled(TEXT::Localize("editor.performance.gpuFrameState").c_str(),
                                MetricText(gpuFrame, 3).c_str());
        }
        ImGui::EndTable();
    }
}

void DrawCpuTimingTable(const RuntimePerformanceSnapshot& s) {
    ImGui::SeparatorText(TEXT::Localize("editor.ui.cpu.timing.cff982").c_str());
    const METRIC::MetricReading* frame = METRIC::FindReading(s.metrics, METRIC::kFrameCpuMs);
    if (!IsFresh(frame)) {
        ImGui::TextDisabled(TEXT::Localize("editor.performance.cpuFrameState").c_str(), MetricText(frame, 3).c_str());
        return;
    }

    // フレームの区間は列挙の順に、残りはシステムごとの計時として p95 の大きい順に並べる。
    const std::vector<METRIC::TimingRow> rows = METRIC::TimingRows(s.metrics, "cpu");
    std::vector<const METRIC::TimingRow*> phases;
    std::vector<const METRIC::TimingRow*> systems;
    for (const METRIC::TimingRow& row : rows) {
        if (IsFresh(row.ms)) {
            (FramePhaseOrder(row.name) >= 0 ? phases : systems).push_back(&row);
        }
    }
    std::sort(phases.begin(), phases.end(), [](const METRIC::TimingRow* lhs, const METRIC::TimingRow* rhs) {
        return FramePhaseOrder(lhs->name) < FramePhaseOrder(rhs->name);
    });
    std::sort(systems.begin(), systems.end(), [](const METRIC::TimingRow* lhs, const METRIC::TimingRow* rhs) {
        if (lhs->ms->window.p95 != rhs->ms->window.p95) {
            return lhs->ms->window.p95 > rhs->ms->window.p95;
        }
        return lhs->ms->window.average > rhs->ms->window.average;
    });

    const double frameMs = (std::max)(0.0001, frame->value);
    ImGui::Text(TEXT::Localize("editor.ui.frame.llu.scopes.u.frame.3f.ms.ea6fdd").c_str(),
                static_cast<unsigned long long>(frame->sourceFrame), static_cast<unsigned>(phases.size() + 1u),
                frameMs);
    ImGui::Text(TEXT::Localize("editor.ui.rolling.u.frames.avg.3f.p95.3f.p99.3f.max.3f.ms.fdf253").c_str(),
                frame->window.sampleCount, frame->window.average, frame->window.p95, frame->window.p99,
                frame->window.maximum);
    ImGui::TextDisabled(TEXT::Localize("editor.ui.inclusive.timings.nested.scopes.are.not.additive.e24523").c_str());

    const auto drawTimingRow = [frameMs](std::string_view name, const METRIC::MetricReading& ms,
                                         const METRIC::MetricReading* calls) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(name.data(), name.data() + name.size());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.3f", ms.value);
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("%.3f", ms.window.average);
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%.3f", ms.window.p95);
        ImGui::TableSetColumnIndex(4);
        ImGui::Text("%.3f", ms.window.p99);
        ImGui::TableSetColumnIndex(5);
        ImGui::Text("%.3f", ms.window.maximum);
        ImGui::TableSetColumnIndex(6);
        ImGui::TextUnformatted(calls != nullptr ? MetricText(calls, 0).c_str() : "-");
        ImGui::TableSetColumnIndex(7);
        ImGui::ProgressBar(static_cast<float>((std::min)(1.0, ms.value / frameMs)), ImVec2(-1.0f, 0.0f), "");
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

        drawTimingRow(CPU_PROFILE::ToString(CPU_PROFILE::Pass::Frame), *frame, nullptr);
        for (const METRIC::TimingRow* row : phases) {
            drawTimingRow(row->name, *row->ms, row->calls);
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText(TEXT::Localize("editor.ui.scene.system.cpu.e8ace7").c_str());
    ImGui::TextDisabled(
        TEXT::Localize("editor.ui.frame.scopes.aggregate.variable.updates.fixed.scopes.aggregate.a.93f7e5").c_str());
    if (systems.empty()) {
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
        for (const METRIC::TimingRow* row : systems) {
            drawTimingRow(row->name, *row->ms, row->calls);
        }
        ImGui::EndTable();
    }
}

void DrawGpuTimingTable(const RuntimePerformanceSnapshot& s) {
    ImGui::SeparatorText(TEXT::Localize("editor.ui.gpu.timing.10adc4").c_str());
    const METRIC::MetricReading* frame = METRIC::FindReading(s.metrics, METRIC::kFrameGpuMs);
    if (!IsFresh(frame)) {
        // 計時できない構成ではその理由を、それ以外は状態を示す。
        if (frame != nullptr && frame->status == METRIC::MetricStatus::Unsupported) {
            ImGui::TextDisabled("%s", std::string(frame->note).c_str());
        } else {
            ImGui::TextDisabled(TEXT::Localize("editor.performance.gpuFrameState").c_str(),
                                MetricText(frame, 3).c_str());
        }
        return;
    }

    std::vector<METRIC::TimingRow> rows = METRIC::TimingRows(s.metrics, "gpu");
    rows.erase(std::remove_if(rows.begin(), rows.end(), [](const METRIC::TimingRow& row) { return !IsFresh(row.ms); }),
               rows.end());
    ImGui::Text(TEXT::Localize("editor.ui.frame.llu.u.measured.passes.bcd895").c_str(),
                static_cast<unsigned long long>(frame->sourceFrame), static_cast<unsigned>(rows.size()));
    ImGui::TextDisabled(
        TEXT::Localize("editor.ui.sample.sums.all.executions.of.a.pass.in.the.frame.and.includes.c.6c68c4").c_str());
    if (ImGui::BeginTable("GpuDrivenTimingTable", 8,
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (const char* title : {"Pass", "Sample ms", "Calls", "Self ms", "Avg ms", "P95 ms", "P99 ms", "Samples"})
            ImGui::TableSetupColumn(title);
        ImGui::TableHeadersRow();
        for (const METRIC::TimingRow& row : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(row.name.data(), row.name.data() + row.name.size());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", row.ms->value);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(MetricText(row.calls, 0).c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(MetricText(row.selfMs, 3).c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.3f", row.ms->window.average);
            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%.3f", row.ms->window.p95);
            ImGui::TableSetColumnIndex(6);
            ImGui::Text("%.3f", row.ms->window.p99);
            ImGui::TableSetColumnIndex(7);
            ImGui::Text("%u", row.ms->window.sampleCount);
        }
        ImGui::EndTable();
    }
}

} // namespace HIKARI::EDITOR::PERFORMANCE_AUDIT
