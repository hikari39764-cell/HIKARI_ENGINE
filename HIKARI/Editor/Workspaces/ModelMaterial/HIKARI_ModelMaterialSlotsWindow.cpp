#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Models/HIKARI_ModelMaterialSlot.h"
#include "Editor/Assets/HIKARI_AssetTrashService.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {

bool ContainsText(std::string_view value, std::string_view query) {
    if (query.empty()) {
        return true;
    }
    return std::search(value.begin(), value.end(), query.begin(), query.end(), [](char left, char right) {
               return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
           }) != value.end();
}

const char* BindingLabel(ModelMaterialBindingSource source) noexcept {
    switch (source) {
    case ModelMaterialBindingSource::ImportedFallback: return TEXT::Localize("ui.assetBrowser.stateImported").c_str();
    case ModelMaterialBindingSource::AuthoredOverride: return TEXT::Localize("editor.ui.authored.026b05").c_str();
    case ModelMaterialBindingSource::OverrideUnavailable: return TEXT::Localize("ui.lightingBake.unavailable").c_str();
    }
    return TEXT::Localize("ui.quality.transitionUnknown").c_str();
}

} // namespace

void ModelMaterialWorkspaceController::DrawModelAndSlotsWindow(DocumentSceneBase& scene) {
    if (!ImGui::Begin(TEXT::LocalizeWindow("editor.ui.materials.parts.efb024", "ModelMaterial/Slots").c_str())) {
        ImGui::End();
        return;
    }

    const float modeButtonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const auto workflowButton = [&](const char* label, AuthoringMode mode) {
        const bool selected = authoringMode_ == mode;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.43f, 0.49f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.51f, 0.58f, 1.0f));
        }
        const bool pressed = ImGui::Button(label, ImVec2(modeButtonWidth, 0.0f));
        if (selected) {
            ImGui::PopStyleColor(2);
        }
        if (pressed && !selected) {
            (void)SwitchAuthoringMode(scene, mode, statusMessage_);
        }
    };
    workflowButton("Model Materials", AuthoringMode::ModelMaterials);
    ImGui::SameLine();
    workflowButton("Material Asset", AuthoringMode::MaterialAsset);
    ImGui::Separator();

    if (authoringMode_ == AuthoringMode::MaterialAsset) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.standalone.material.19e03a").c_str());
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.create.or.open.one.material.asset.surface.materials.use.the.neut.17e19e")
                .c_str());

        std::string openGuid = candidateAuthoringAssetGuid_.value;
        AssetFieldOptions openOptions{};
        openOptions.label = "Open Material";
        openOptions.requiredType = AssetType::Material;
        if (DrawAssetField(&scene.GetAssetDatabase(), openOptions, openGuid)) {
            const AssetGuid selectedGuid{openGuid};
            if (selectedGuid.IsValid()) {
                candidateAuthoringAssetGuid_ = selectedGuid;
                (void)OpenMaterialAsset(scene, selectedGuid, statusMessage_);
            }
        }

        const auto& templates = ASSETS::MATERIAL::GetBuiltInMaterialTemplates();
        const auto selectedTemplate = std::find_if(templates.begin(), templates.end(),
                                                   [this](const auto& entry) { return entry.id == selectedTemplate_; });
        const char* templateLabel =
            selectedTemplate != templates.end() ? selectedTemplate->displayName.data() : "Material Template";
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##StandaloneMaterialName",
                                 TEXT::Localize("editor.ui.new.material.name.be45f0").c_str(), newMaterialName_.data(),
                                 newMaterialName_.size());
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##StandaloneMaterialTemplate", templateLabel)) {
            for (const auto& entry : templates) {
                if (ImGui::Selectable(entry.displayName.data(), selectedTemplate_ == entry.id)) {
                    selectedTemplate_ = entry.id;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                    ImGui::SetTooltip("%s", entry.description.data());
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.create.material.5342da", "Create Material").c_str(),
                          ImVec2(-1.0f, 0.0f))) {
            (void)CreateMaterial(scene, selectedTemplate_, false, statusMessage_);
        }

        ImGui::SeparatorText(TEXT::Localize("editor.ui.project.materials.bfaa56").c_str());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##StandaloneMaterialSearch",
                                 TEXT::Localize("editor.ui.filter.materials.fc64cb").c_str(), materialSearch_.data(),
                                 materialSearch_.size());
        const std::string_view query(materialSearch_.data());
        if (ImGui::BeginChild("##StandaloneMaterialList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border)) {
            for (const AssetChoice& choice : materialChoices_) {
                if (choice.materialKind != MaterialAssetKind::Material &&
                    choice.materialKind != MaterialAssetKind::Instance) {
                    continue;
                }
                if (!ContainsText(choice.label, query) && !ContainsText(choice.guid.value, query)) {
                    continue;
                }
                const bool selected = choice.guid == authoringDocument_.GetMaterialAssetGuid();
                if (ImGui::Selectable(choice.label.c_str(), selected)) {
                    candidateAuthoringAssetGuid_ = choice.guid;
                    (void)OpenMaterialAsset(scene, choice.guid, statusMessage_);
                }
            }
        }
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    const char* modelPreview = modelDisplayName_.empty() ? "Select a model..." : modelDisplayName_.c_str();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##ModelMaterialModel", modelPreview)) {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##ModelMaterialModelSearch", TEXT::Localize("editor.ui.filter.models.a821e5").c_str(),
                                 modelSearch_.data(), modelSearch_.size());
        ImGui::Separator();
        const std::string_view query(modelSearch_.data());
        for (const AssetChoice& choice : modelChoices_) {
            if (!ContainsText(choice.label, query) && !ContainsText(choice.guid.value, query)) {
                continue;
            }
            ImGui::PushID(choice.guid.value.c_str());
            const std::string label = choice.label + (choice.ready ? "" : "  [import required]");
            if (ImGui::Selectable(label.c_str(), choice.guid == modelGuid_)) {
                (void)OpenModel(scene, choice.guid, statusMessage_);
            }
            ImGui::PopID();
        }
        if (modelChoices_.empty()) {
            ImGui::TextDisabled(TEXT::Localize("editor.ui.no.model.assets.are.registered.255b81").c_str());
        }
        ImGui::EndCombo();
    }

    if (!IsEditingModel()) {
        ImGui::Spacing();
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.choose.an.imported.model.the.workspace.consumes.its.hmodel.hcmes.985ce2")
                .c_str());
        ImGui::End();
        return;
    }

    const AssetGuid activeSetGuid = authoringDocument_.GetBindingAssetGuid();
    const AssetChoice* activeSet = nullptr;
    for (const AssetChoice& choice : materialSetChoices_) {
        if (choice.guid == activeSetGuid) {
            activeSet = &choice;
            break;
        }
    }
    const char* setPreview = activeSet != nullptr ? activeSet->label.c_str() : "Unsaved Material Set";
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##ModelMaterialSet", setPreview)) {
        for (const AssetChoice& choice : materialSetChoices_) {
            if (choice.ownerModelGuid != modelGuid_) {
                continue;
            }
            if (ImGui::Selectable(choice.label.c_str(), choice.guid == activeSetGuid)) {
                (void)OpenMaterialSet(scene, choice.guid, statusMessage_);
            }
        }
        ImGui::Separator();
        if (ImGui::Selectable(
                TEXT::LocalizeWindow("editor.ui.new.material.set.f8896f", "+ New Material Set").c_str())) {
            (void)CreateMaterialSet(scene, statusMessage_);
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(!authoringDocument_.IsBindingDirty());
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.save.set.b472eb", "Save Set").c_str())) {
        (void)SaveBindings(scene, false, statusMessage_);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.save.set.as.fcd87e", "Save Set As...").c_str())) {
        ImGui::OpenPopup("SaveBindingAsPopupFromParts");
    }
    if (ImGui::BeginPopup("SaveBindingAsPopupFromParts")) {
        ImGui::TextUnformatted(TEXT::Localize("editor.ui.path.under.assets.9905f5").c_str());
        ImGui::SetNextItemWidth(300.0f);
        ImGui::InputText("##BindingSaveAsPath", bindingSaveAsPath_.data(), bindingSaveAsPath_.size());
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.save.copy.ec185a", "Save Copy").c_str()) &&
            SaveBindings(scene, true, statusMessage_)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+##NewMaterialSet")) {
        (void)CreateMaterialSet(scene, statusMessage_);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(TEXT::Localize("editor.ui.create.another.material.set.for.this.model.d17f86").c_str());
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!authoringDocument_.GetBindingAssetGuid().IsValid());
    if (ImGui::SmallButton(TEXT::LocalizeWindow("editor.ui.trash.e3bf62", "Trash##DeleteMaterialSet").c_str())) {
        ImGui::OpenPopup(
            TEXT::LocalizeWindow("editor.ui.deletecurrentmaterialsetpopup.7094dc", "DeleteCurrentMaterialSetPopup")
                .c_str());
    }
    ImGui::EndDisabled();
    if (ImGui::BeginPopupModal(
            TEXT::LocalizeWindow("editor.ui.deletecurrentmaterialsetpopup.7094dc", "DeleteCurrentMaterialSetPopup")
                .c_str(),
            nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const AssetGuid setGuid = authoringDocument_.GetBindingAssetGuid();
        const AssetRecord* setRecord = setGuid.IsValid() ? scene.GetAssetDatabase().FindByGuid(setGuid) : nullptr;
        ImGui::TextUnformatted(
            TEXT::Localize("editor.ui.move.this.model.material.set.to.project.trash.47fff4").c_str());
        if (setRecord != nullptr) {
            ImGui::TextWrapped("%s", setRecord->sourcePath.generic_string().c_str());
        }
        if (authoringDocument_.IsBindingDirty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                               TEXT::Localize("editor.ui.unsaved.set.edits.will.be.discarded.cbfee0").c_str());
        }
        const AssetTrashReferenceAnalysis references = AnalyzeAssetTrashReferences(scene.GetAssetDatabase(), setGuid);
        if (references.IsReferenced()) {
            ImGui::SeparatorText(TEXT::Localize("editor.ui.references.5d20d0").c_str());
            ImGui::TextColored(
                ImVec4(1.0f, 0.48f, 0.34f, 1.0f),
                TEXT::Localize("editor.ui.this.material.set.is.still.selected.by.a.scene.and.cannot.be.del.d487d7")
                    .c_str());
            for (size_t index = 0u; index < (std::min)(references.references.size(), size_t{3u}); ++index) {
                const AssetTrashReference& reference = references.references[index];
                ImGui::BulletText("%s (%s)", reference.ownerSourcePath.generic_string().c_str(),
                                  reference.role.c_str());
            }
        }
        ImGui::BeginDisabled(references.IsReferenced());
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.move.to.trash.dffd3b", "Move to Trash").c_str(),
                          ImVec2(120.0f, 0.0f))) {
            const AssetTrashResult deleted = MoveAssetToProjectTrash(scene.GetAssetDatabase(), setGuid);
            statusMessage_ = deleted.message;
            if (deleted.success) {
                if (!scene.ReloadAssets()) {
                    statusMessage_ += "; Registry publication failed";
                } else {
                    assetChoiceRevision_ = 0u;
                    (void)LoadOrCreateBindings(scene, statusMessage_);
                    RebuildSlotState(scene.GetAssetDatabase(), scene.GetAssetRegistry());
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.common.cancel", "Cancel").c_str(), ImVec2(100.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::Text(TEXT::Localize("editor.ui.u.material.slots.2dab54").c_str(), stats_.materialSlotCount);
    ImGui::SameLine();
    if (authoringDocument_.IsBindingDirty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.3f, 1.0f),
                           TEXT::Localize("editor.ui.unsaved.binding.edits.de2eb6").c_str());
    }
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##ModelMaterialSlotSearch",
                             TEXT::Localize("editor.ui.filter.name.or.slotid.5b4479").c_str(), slotSearch_.data(),
                             slotSearch_.size());
    ImGui::Separator();

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##ModelMaterialSlots", 4, flags)) {
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 84.0f);
        ImGui::TableSetupColumn("Surfaces", ImGuiTableColumnFlags_WidthFixed, 62.0f);
        ImGui::TableHeadersRow();
        const std::string_view query(slotSearch_.data());
        for (const SlotState& slot : slots_) {
            const std::string slotId = ASSETS::MODELS::MaterialSlotIdToString(slot.view.slotId);
            if (!ContainsText(slot.view.displayName, query) && !ContainsText(slotId, query)) {
                continue;
            }
            ImGui::PushID(static_cast<int>(slot.view.materialIndex));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const bool selected = selectedMaterialIndex_ && *selectedMaterialIndex_ == slot.view.materialIndex;
            const std::string id = std::to_string(slot.view.materialIndex) + "##SelectMaterialSlot";
            if (ImGui::Selectable(id.c_str(), selected,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
                (void)SelectSlotForEditing(scene, slot.view.materialIndex, statusMessage_);
            }
            if (selected) {
                ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                                    IM_COL32(80, 220, 232, 255), 3.0f, 0, 2.0f);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
                ImGui::SetTooltip(TEXT::Localize("editor.ui.stable.slotid.s.b972aa").c_str(),
                                  slotId.empty() ? "missing" : slotId.c_str());
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(slot.view.displayName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(BindingLabel(slot.view.source));
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%u", slot.view.surfaceCount);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

} // namespace HIKARI::EDITOR
