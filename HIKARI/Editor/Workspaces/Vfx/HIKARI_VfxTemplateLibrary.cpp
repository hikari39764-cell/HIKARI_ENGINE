#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Core/Text/HIKARI_Localization.h"

#include "imgui.h"

namespace HIKARI::EDITOR {
void VfxWorkspaceController::DrawTemplateLibrary() {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##TemplateFilter", VfxUiText("Search templates").c_str(), templateSearch_.data(),
                             templateSearch_.size());
    const ImGuiTextFilter filter(templateSearch_.data());
    if (ImGui::BeginTable("##TemplateColumns", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn(TEXT::Localize("vfx.templates.complete").c_str());
        ImGui::TableSetupColumn(TEXT::Localize("vfx.templates.modules").c_str());
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();
        for (int column = 0; column < 2; ++column) {
            ImGui::TableSetColumnIndex(column);
            ImGui::PushID(column);
            std::string previousCategory;
            for (const auto& preset : VFX::PRESETS::GetVfxPresetCatalog()) {
                if (column == 0 && preset.category != "Compositions")
                    continue;
                if (column == 1 && preset.category == "Compositions" && preset.id != "ribbon_trail" &&
                    preset.id != "shockwave" && preset.id != "charge")
                    continue;
                const auto name = VfxUiText(preset.displayName);
                if (!filter.PassFilter((name + " " + preset.id).c_str()))
                    continue;
                if (column == 1 && previousCategory != preset.category) {
                    ImGui::SeparatorText(VfxUiText(preset.category).c_str());
                    previousCategory = preset.category;
                }
                ImGui::PushID(preset.id.c_str());
                if (IconTextButton(column == 0 ? EditorGlyph::Vfx : EditorGlyph::Add, name.c_str(), "UseTemplate",
                                   EditorButtonTone::Quiet)) {
                    if (column == 0)
                        newTemplateId_ = preset.id;
                    else
                        (void)AddPresetLayers(preset.id, statusMessage_);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", VfxUiText(preset.summary).c_str());
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!newTemplateId_.empty()) {
        if (!document_.IsDirty()) {
            (void)NewAssetFromPreset(newTemplateId_, statusMessage_);
            newTemplateId_.clear();
        } else
            ImGui::OpenPopup((VfxUiText("Replace unsaved effect?") + "###VfxReplace").c_str());
    }
    if (ImGui::BeginPopupModal(
            (VfxUiText(TEXT::Localize("ui.vfx.text.Replace unsaved effect?").c_str()) + "###VfxReplace").c_str(),
            nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            VfxUiText("The current effect has unsaved changes. Save it first to keep those edits.").c_str());
        if (ImGui::Button(VfxUiText("Cancel").c_str())) {
            newTemplateId_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(VfxUiText("Discard and create").c_str())) {
            (void)NewAssetFromPreset(newTemplateId_, statusMessage_);
            newTemplateId_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
} // namespace HIKARI::EDITOR
