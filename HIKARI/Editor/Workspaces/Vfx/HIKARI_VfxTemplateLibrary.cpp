#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    void VfxWorkspaceController::DrawTemplateLibrary() {
#if defined(HIKARI_WITH_EDITOR)
        ImGui::TextWrapped(VfxUiText("Build an effect by combining editable layers. Add sparks, smoke, rings or real 3D geometry.").c_str());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##TemplateFilter", VfxUiText("Search templates").c_str(), templateSearch_.data(), templateSearch_.size());
        const ImGuiTextFilter filter(templateSearch_.data());
        for (const auto& preset : VFX::PRESETS::GetVfxPresetCatalog()) {
            const std::string searchable = VfxUiText(preset.displayName) + " " + VfxUiText(preset.category) + " " + VfxUiText(preset.summary) + " " + preset.id;
            if (!filter.PassFilter(searchable.c_str())) continue;
            ImGui::PushID(preset.id.c_str());
            ImGui::SeparatorText(VfxUiText(preset.displayName).c_str());
            ImGui::TextDisabled("%s", VfxUiText(preset.category).c_str());
            ImGui::TextWrapped("%s", VfxUiText(preset.summary).c_str());
            if (ImGui::Button(VfxUiText("+ Add layers").c_str())) {
                (void)AddPresetLayers(preset.id, statusMessage_);
            }
            ImGui::SameLine();
            if (ImGui::Button(VfxUiText("New effect").c_str())) newTemplateId_ = preset.id;
            ImGui::PopID();
        }
        if (!newTemplateId_.empty()) {
            if (!document_.IsDirty()) {
                (void)NewAssetFromPreset(newTemplateId_, statusMessage_);
                newTemplateId_.clear();
            } else ImGui::OpenPopup((VfxUiText("Replace unsaved effect?") + "###VfxReplace").c_str());
        }
        if (ImGui::BeginPopupModal((VfxUiText("Replace unsaved effect?") + "###VfxReplace").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped(VfxUiText("The current effect has unsaved changes. Save it first to keep those edits.").c_str());
            if (ImGui::Button(VfxUiText("Cancel").c_str())) { newTemplateId_.clear(); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button(VfxUiText("Discard and create").c_str())) {
                (void)NewAssetFromPreset(newTemplateId_, statusMessage_);
                newTemplateId_.clear(); ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::Spacing();
        ImGui::TextWrapped(VfxUiText("New > template starts a separate effect. Adding layers keeps this effect's duration, loop mode and existing layers.").c_str());
#endif
    }
}
