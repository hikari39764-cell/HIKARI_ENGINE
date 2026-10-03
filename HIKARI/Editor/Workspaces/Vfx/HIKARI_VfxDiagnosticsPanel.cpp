#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <string>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
// AI 消費者のための現在状態の要約。どの資産を開いていて、どの層があり、
// どの効果とキーが載り、どこを選び、プレビューがどこにいるかを 1 つの
// テキストへまとめる。診断と同じく「今の真実」を所有するのはこの場所。
bool VfxWorkspaceController::IsDocumentOpen() const noexcept { return GetDocument().IsOpen(); }

nlohmann::json VfxWorkspaceController::DescribeDocumentState() const {
    const VfxEditorDocument& document = GetDocument();
    nlohmann::json state{{"open", IsDocumentOpen()},
                         {"assetGuid", document.GetAssetGuid().value},
                         {"dirty", document.IsDirty()},
                         {"revision", document.GetRevision()}};
    const VFX::VfxAssetData* asset = GetAsset();
    if (asset == nullptr) {
        return state;
    }
    state["name"] = asset->name;
    std::string status = "vfx: " + asset->name + " | " + std::to_string(asset->durationSeconds) + "s" +
                         (asset->loop ? " loop" : "") + " | layers " + std::to_string(asset->emitters.size());
    for (const VFX::VfxEmitterData& layer : asset->emitters) {
        status += "\n  [" + layer.id + "] " + layer.name + " ";
        status += VFX::ToString(layer.kind);
        if (layer.kind != VFX::VfxLayerKind::Group && layer.kind != VFX::VfxLayerKind::Adjustment) {
            status += " ";
            status += std::string(VFX::ToString(layer.renderShape.type));
            status +=
                layer.renderShape.uvMode == VFX::VfxShapeUvMode::WorldLength ? " uv=world-length" : " uv=parameter";
            if (layer.renderShape.uvSwapAxes)
                status += " uv-swapped";
        }
        if (!layer.effects.empty()) {
            status += " | effects:";
            for (const MaterialEffectStep& effect : layer.effects) {
                status += " ";
                status += std::string(MaterialEffectName(effect.kind));
                if (!effect.enabled)
                    status += "(disabled)";
            }
        }
        if (!layer.parameterTracks.empty()) {
            status += " | parameter keys: ";
            status += std::to_string(layer.parameterTracks.size());
        }
    }
    const VfxWorkspaceSelection& selection = GetSelection();
    if (!selection.emitterId.empty()) {
        status += "\nselection: " + selection.emitterId;
        if (!selection.propertyId.empty()) {
            status += " " + selection.propertyId;
        }
    } else {
        status += "\nselection: " + selection.targetId;
    }
    status += "\npreview: " + std::to_string(preview_.GetPreviewElapsedSeconds()) + "s ";
    status += preview_.IsPreviewPlaying() ? (preview_.IsPreviewPaused() ? "paused" : "playing") : "stopped";
    state["status"] = std::move(status);
    return state;
}

namespace {
#if defined(HIKARI_WITH_EDITOR)
const char* SeverityLabel(VFX::VfxDiagnosticSeverity severity) noexcept {
    switch (severity) {
    case VFX::VfxDiagnosticSeverity::Info: return TEXT::LocalizeCStr("log.info");
    case VFX::VfxDiagnosticSeverity::Warning: return TEXT::LocalizeCStr("editor.ui.warning.e9c455");
    case VFX::VfxDiagnosticSeverity::Error: return TEXT::LocalizeCStr("ui.assetBrowser.stateError");
    }
    return TEXT::LocalizeCStr("ui.quality.transitionUnknown");
}

ImVec4 SeverityColor(VFX::VfxDiagnosticSeverity severity) noexcept {
    switch (severity) {
    case VFX::VfxDiagnosticSeverity::Info: return {0.48f, 0.72f, 1.0f, 1.0f};
    case VFX::VfxDiagnosticSeverity::Warning: return {1.0f, 0.72f, 0.22f, 1.0f};
    case VFX::VfxDiagnosticSeverity::Error: return {1.0f, 0.34f, 0.30f, 1.0f};
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}
#endif
} // namespace

void VfxWorkspaceController::DrawDiagnostics() {
#if defined(HIKARI_WITH_EDITOR)
    if (preview_.diagnostics_.empty()) {
        ImGui::TextColored(
            {0.42f, 0.88f, 0.55f, 1.0f},
            TEXT::Localize("editor.ui.the.current.vfx.source.compiles.without.diagnostics.ffd1e4").c_str());
        return;
    }
    if (!ImGui::BeginTable("VfxDiagnosticsTable", 3,
                           ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)) {
        return;
    }
    ImGui::TableSetupColumn("Severity", ImGuiTableColumnFlags_WidthFixed, 78.0f);
    ImGui::TableSetupColumn("Code", ImGuiTableColumnFlags_WidthFixed, 180.0f);
    ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for (size_t index = 0u; index < preview_.diagnostics_.size(); ++index) {
        const VFX::VfxDiagnostic& diagnostic = preview_.diagnostics_[index];
        ImGui::PushID(static_cast<int>(index));
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextColored(SeverityColor(diagnostic.severity), "%s", SeverityLabel(diagnostic.severity));
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(diagnostic.code.c_str());
        ImGui::TableSetColumnIndex(2);
        if (ImGui::Selectable(diagnostic.message.c_str(), false, ImGuiSelectableFlags_SpanAllColumns) &&
            !diagnostic.subjectId.empty()) {
            if (const VFX::VfxEmitterData* emitter = FindEmitter(diagnostic.subjectId)) {
                Select({VfxWorkspaceSelectionKind::Emitter, emitter->id, emitter->id});
            } else if (FindInput(diagnostic.subjectId) != nullptr) {
                Select({VfxWorkspaceSelectionKind::Input, diagnostic.subjectId});
            } else if (const VFX::VfxEmitterData* owner = nullptr;
                       FindInputLink(diagnostic.subjectId, &owner) != nullptr && owner != nullptr) {
                Select({VfxWorkspaceSelectionKind::Link, diagnostic.subjectId, owner->id});
            }
        }
        if (ImGui::IsItemHovered() && !diagnostic.subjectId.empty()) {
            ImGui::SetTooltip(TEXT::Localize("editor.ui.subject.s.091ab5").c_str(), diagnostic.subjectId.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
#endif
}

} // namespace HIKARI::EDITOR
