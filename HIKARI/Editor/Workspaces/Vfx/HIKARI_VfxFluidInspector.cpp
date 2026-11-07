#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

#include <imgui.h>

#include "Assets/Vfx/HIKARI_VfxProgram.h"
#include "Authoring/HIKARI_FieldInspector.h"
#include "Core/HIKARI_TimeService.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Diagnostics/HIKARI_MetricRegistry.h"
#include "Editor/Inspectors/HIKARI_ImGuiInspectorBuilder.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Vfx/HIKARI_FluidVolumePass.h"

// 流体の層（域・源）の検査器と、域の解算の統計・調試切片。域と源の値は字段表から描き（資産の読み書きと同じ表）、
// 変わった項目だけを SetProperty で書く（撤销・拖动の合併・智能体の読み書きと同じ道）。統計と切片はプレビューの
// 実例の域を読む。
namespace HIKARI::EDITOR {
namespace {
namespace FLUID = RENDER3D::FLUID;

constexpr std::array<const char*, 4> kSliceFieldLabels{"Density", "Velocity", "Divergence", "Pressure"};
constexpr std::array<const char*, 3> kSliceAxisLabels{"X", "Y", "Z"};

template <class T, class Fields>
void DrawFluidFields(VfxWorkspaceController& controller, AssetDatabase& database, const std::string& moduleId,
                     const T& value, const Fields& fields, std::string& status) {
    ImGuiInspectorBuilder builder;
    InspectorContext context{};
    context.assetDatabase = &database;
    builder.SetContext(context);
    T edited = value;
    ImGui::PushID(moduleId.c_str());
    if (InspectFields(builder, edited, fields)) {
        nlohmann::json before = nlohmann::json::object();
        nlohmann::json after = nlohmann::json::object();
        JSONFIELDS::Write(before, value, fields);
        JSONFIELDS::Write(after, edited, fields);
        for (auto field = after.begin(); field != after.end(); ++field) {
            if (before[field.key()] != field.value()) {
                (void)controller.SetProperty(moduleId, field.key(), field.value(), status,
                                             VfxInspectorPropertyEditor::MergeGroup(moduleId, field.key()), &database);
            }
        }
    }
    ImGui::PopID();
    // ドラッグが終わったら合併を閉じる（次の編集は別の取り消し単位）。
    if (!ImGui::IsAnyItemActive()) {
        controller.SealPropertyEdit();
    }
}

double MetricValue(const std::vector<METRIC::MetricReading>& readings, std::string_view id) {
    const METRIC::MetricReading* reading = METRIC::FindReading(readings, id);
    return reading != nullptr ? reading->value : 0.0;
}

nlohmann::json DescribeStats(const FLUID::FluidDomainStats& stats) {
    return {
        {"cells", {stats.cells[0], stats.cells[1], stats.cells[2]}},
        {"levels", stats.levels},
        {"bytes", stats.bytes},
        {"completedSteps", stats.completedSteps},
        {"stepsThisFrame", stats.stepsThisFrame},
        {"prewarmRemainingSeconds", stats.prewarmRemainingSeconds},
        {"activeBricks", stats.activeBricks},
        {"totalBricks", stats.totalBricks},
        {"diagnosticsValid", stats.diagnosticsValid},
        {"maxDivergence", stats.maxDivergence},
        {"densityTotal", stats.densityTotal},
    };
}
} // namespace

const FLUID::FluidDomainStats* VfxWorkspaceController::FindPreviewFluidStats(std::string_view layerId) const {
    const std::shared_ptr<const VFX::VfxProgram>& program = preview_.previewProgram_;
    if (!program) {
        return nullptr;
    }
    const auto layer = std::find_if(
        program->emitters.begin(), program->emitters.end(), [layerId](const VFX::VfxProgramEmitter& emitter) {
            return emitter.kind == VFX::VfxLayerKind::Fluid && emitter.sourceEmitterId == layerId;
        });
    if (layer == program->emitters.end()) {
        return nullptr;
    }
    const uint32_t index = static_cast<uint32_t>(layer - program->emitters.begin());
    const uint64_t playbackNamespace = preview_.playbackService_.GetPlaybackNamespace();
    for (const FLUID::FluidDomainStats& domain : MESHRENDERER::GetFluidVolumePassStats().perDomain) {
        if (domain.key.playbackNamespace == playbackNamespace && domain.key.emitterIndex == index) {
            return &domain;
        }
    }
    return nullptr;
}

nlohmann::json VfxWorkspaceController::DescribePreviewFluidStats() const {
    nlohmann::json domains = nlohmann::json::array();
    const VFX::VfxAssetData* asset = GetAsset();
    if (asset == nullptr) {
        return {{"domains", std::move(domains)}};
    }
    const std::vector<METRIC::MetricReading> readings = METRIC::Snapshot(TIME::GetFrameContext().frameIndex);
    for (const VFX::VfxEmitterData& layer : asset->emitters) {
        if (layer.kind != VFX::VfxLayerKind::Fluid) {
            continue;
        }
        nlohmann::json entry{{"layerId", layer.id}, {"name", layer.name}};
        const FLUID::FluidDomainStats* stats = FindPreviewFluidStats(layer.id);
        entry["simulated"] = stats != nullptr;
        if (stats != nullptr) {
            entry["stats"] = DescribeStats(*stats);
        }
        domains.push_back(std::move(entry));
    }
    return {{"domains", std::move(domains)},
            {"gpuSimulationMs", MetricValue(readings, "gpu.Fluid Simulation.ms")},
            {"gpuRenderMs", MetricValue(readings, "gpu.Fluid Render.ms")}};
}

bool VfxWorkspaceController::ShowFluidDebugSlice(std::string_view layerId, bool shown, uint32_t field, uint32_t axis,
                                                 float position, std::string& outMessage) {
    const VFX::VfxEmitterData* layer = FindEmitter(layerId);
    if (layer == nullptr || layer->kind != VFX::VfxLayerKind::Fluid) {
        outMessage = "Fluid domain layer was not found";
        return false;
    }
    Select({VfxWorkspaceSelectionKind::Emitter, layer->id, layer->id});
    fluidSlice_ = {shown, (std::min)(field, static_cast<uint32_t>(kSliceFieldLabels.size() - 1u)), (std::min)(axis, 2u),
                   std::clamp(position, 0.0f, 1.0f)};
    outMessage = shown ? "Fluid debug slice shown" : "Fluid debug slice hidden";
    return true;
}

void VfxWorkspaceController::DrawFluidSimulation(const VFX::VfxEmitterData& domain) {
    // 診断（散度・密度の総量）は見ている間だけ回読する。
    MESHRENDERER::RequestFluidDiagnostics();
    const FLUID::FluidDomainStats* stats = FindPreviewFluidStats(domain.id);
    if (stats == nullptr) {
        ImGui::TextWrapped("%s", VfxUiText("Play the preview to simulate this domain.").c_str());
        return;
    }
    const std::vector<METRIC::MetricReading> readings = METRIC::Snapshot(TIME::GetFrameContext().frameIndex);
    if (ImGui::BeginTable("FluidStats", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        const auto row = [](const char* label, const std::string& value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(VfxUiText(label).c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(value.c_str());
        };
        const auto number = [](double value, int digits) {
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
            return std::string(buffer);
        };
        row("Grid", std::to_string(stats->cells[0]) + " x " + std::to_string(stats->cells[1]) + " x " +
                        std::to_string(stats->cells[2]) + "  (" + std::to_string(stats->levels) + ")");
        row("GPU memory", number(static_cast<double>(stats->bytes) / (1024.0 * 1024.0), 1) + " MiB");
        row("Substeps", std::to_string(stats->stepsThisFrame) + " / " + std::to_string(stats->completedSteps));
        if (stats->prewarmRemainingSeconds > 0.0f) {
            row("Prewarm remaining", number(stats->prewarmRemainingSeconds, 2) + " s");
        }
        row("Occupied bricks", std::to_string(stats->activeBricks) + " / " + std::to_string(stats->totalBricks));
        if (stats->diagnosticsValid) {
            row("Max divergence", number(stats->maxDivergence, 4) + " 1/s");
            row("Density total", number(stats->densityTotal, 1));
        }
        row("GPU simulation", number(MetricValue(readings, "gpu.Fluid Simulation.ms"), 3) + " ms");
        row("GPU render", number(MetricValue(readings, "gpu.Fluid Render.ms"), 3) + " ms");
        ImGui::EndTable();
    }

    ImGui::SeparatorText(VfxUiText("Debug slice").c_str());
    ImGui::Checkbox(VfxUiText("Show slice").c_str(), &fluidSlice_.shown);
    if (!fluidSlice_.shown) {
        return;
    }
    const std::string fieldLabel = VfxUiText(kSliceFieldLabels[fluidSlice_.field]);
    if (ImGui::BeginCombo(VfxUiText("Field").c_str(), fieldLabel.c_str())) {
        for (uint32_t index = 0u; index < kSliceFieldLabels.size(); ++index) {
            if (ImGui::Selectable(VfxUiText(kSliceFieldLabels[index]).c_str(), index == fluidSlice_.field)) {
                fluidSlice_.field = index;
            }
        }
        ImGui::EndCombo();
    }
    int axis = static_cast<int>(fluidSlice_.axis);
    if (ImGui::Combo(VfxUiText("Axis").c_str(), &axis, kSliceAxisLabels.data(),
                     static_cast<int>(kSliceAxisLabels.size()))) {
        fluidSlice_.axis = static_cast<uint32_t>(axis);
    }
    ImGui::SliderFloat(VfxUiText("Position").c_str(), &fluidSlice_.position, 0.0f, 1.0f);
    FLUID::FluidDebugSliceRequest request{};
    request.key = stats->key;
    request.field = static_cast<FLUID::FluidDebugField>(fluidSlice_.field);
    request.axis = fluidSlice_.axis;
    request.position = fluidSlice_.position;
    request.visualize = true;
    MESHRENDERER::RequestFluidDebugSlice(request);
    const FLUID::FluidDebugSliceView slice = MESHRENDERER::GetFluidDebugSlice();
    if (!slice.valid || slice.width == 0u || slice.height == 0u) {
        return;
    }
    // 格子の上（+Y、Z の切片は +Y、Y の切片は +Z）を画面の上にする。
    const float width = (std::max)(ImGui::GetContentRegionAvail().x, 64.0f);
    const float height = width * static_cast<float>(slice.height) / static_cast<float>(slice.width);
    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(slice.srv.ptr)), ImVec2(width, height),
                 ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
}

void VfxWorkspaceController::DrawFluidLayerInspector(AssetDatabase& database, const VFX::VfxEmitterData& emitter) {
    const bool domain = emitter.kind == VFX::VfxLayerKind::Fluid;
    const bool injection = !domain && FindFluidDomainUnder(emitter.parentEmitterId) == nullptr;
    ImGui::TextWrapped("%s", emitter.name.c_str());
    ImGui::TextDisabled("%s", TEXT::Localize(domain      ? "ui.vfx.layer.newFluid"
                                             : injection ? "ui.vfx.layer.newFluidInjection"
                                                         : "ui.vfx.layer.newFluidSource")
                                  .c_str());
    if (injection)
        ImGui::TextWrapped("%s", TEXT::Localize("ui.vfx.fluid.injectionHint").c_str());
    ImGui::Separator();
    DrawEmitterTiming(database, emitter);
    if (domain) {
        if (ImGui::CollapsingHeader(VfxUiText("Fluid domain").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawFluidFields(*this, database, emitter.fluidDomain.id,
                            static_cast<const FLUID::FluidDomainSettings&>(emitter.fluidDomain),
                            FLUID::kFluidDomainSettingsFields, statusMessage_);
        }
        if (ImGui::CollapsingHeader(VfxUiText("Simulation").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawFluidSimulation(emitter);
        }
        return;
    }
    if (ImGui::CollapsingHeader(VfxUiText("Fluid source").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        // 注入量は spawn の率（密度 / 秒）と一度に注入する密度（burst）。
        VfxInspectorPropertyEditor spawn(*this, database, emitter.spawn.id, statusMessage_);
        spawn.Float("Density / second", "ratePerSecond", emitter.spawn.ratePerSecond, 0.1f, 0.0f, 100000.0f);
        DrawInitialBurst(emitter);
        DrawFluidFields(*this, database, emitter.fluidSource.id,
                        static_cast<const FLUID::FluidSourceSettings&>(emitter.fluidSource),
                        FLUID::kFluidSourceSettingsFields, statusMessage_);
    }
    if (ImGui::CollapsingHeader(VfxUiText("Emission volume").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        DrawEmissionVolume(database, emitter);
    }
    if (ImGui::CollapsingHeader(VfxUiText("Forces").c_str())) {
        DrawForceFields(database, emitter);
    }
}

} // namespace HIKARI::EDITOR
