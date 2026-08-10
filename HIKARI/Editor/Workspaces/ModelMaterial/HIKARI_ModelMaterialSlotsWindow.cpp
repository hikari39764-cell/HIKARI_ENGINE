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

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

bool ContainsText(std::string_view value, std::string_view query) {
  if (query.empty()) {
    return true;
  }
  return std::search(value.begin(), value.end(), query.begin(), query.end(),
                     [](char left, char right) {
                       return std::tolower(static_cast<unsigned char>(left)) ==
                              std::tolower(static_cast<unsigned char>(right));
                     }) != value.end();
}

const char *BindingLabel(ModelMaterialBindingSource source) noexcept {
  switch (source) {
  case ModelMaterialBindingSource::ImportedFallback:
    return "Imported";
  case ModelMaterialBindingSource::AuthoredOverride:
    return "Authored";
  case ModelMaterialBindingSource::OverrideUnavailable:
    return "Unavailable";
  }
  return "Unknown";
}

} // namespace

void ModelMaterialWorkspaceController::DrawModelAndSlotsWindow(
    DocumentSceneBase &scene) {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Materials & Parts###ModelMaterial/Slots")) {
    ImGui::End();
    return;
  }

  const float modeButtonWidth =
      (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) *
      0.5f;
  const auto workflowButton = [&](const char *label, AuthoringMode mode) {
    const bool selected = authoringMode_ == mode;
    if (selected) {
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImVec4(0.18f, 0.43f, 0.49f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImVec4(0.22f, 0.51f, 0.58f, 1.0f));
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
    ImGui::TextDisabled("STANDALONE MATERIAL");
    ImGui::TextWrapped(
        "Create or open one Material Asset. Surface materials use the neutral "
        "sphere and can be assigned to a model part; Particle materials are "
        "previewed and assigned from the VFX workspace.");

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

    const auto &templates = ASSETS::MATERIAL::GetBuiltInMaterialTemplates();
    const auto selectedTemplate = std::find_if(
        templates.begin(), templates.end(), [this](const auto &entry) {
          return entry.id == selectedTemplate_;
        });
    const char *templateLabel = selectedTemplate != templates.end()
                                    ? selectedTemplate->displayName.data()
                                    : "Material Template";
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##StandaloneMaterialName", "New material name",
                             newMaterialName_.data(), newMaterialName_.size());
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##StandaloneMaterialTemplate", templateLabel)) {
      for (const auto &entry : templates) {
        if (ImGui::Selectable(entry.displayName.data(),
                              selectedTemplate_ == entry.id)) {
          selectedTemplate_ = entry.id;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
          ImGui::SetTooltip("%s", entry.description.data());
        }
      }
      ImGui::EndCombo();
    }
    if (ImGui::Button("Create Material", ImVec2(-1.0f, 0.0f))) {
      (void)CreateMaterial(scene, selectedTemplate_, false, statusMessage_);
    }

    ImGui::SeparatorText("Project Materials");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##StandaloneMaterialSearch",
                             "Filter materials...", materialSearch_.data(),
                             materialSearch_.size());
    const std::string_view query(materialSearch_.data());
    if (ImGui::BeginChild("##StandaloneMaterialList", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_Border)) {
      for (const AssetChoice &choice : materialChoices_) {
        if (choice.materialKind != MaterialAssetKind::Material &&
            choice.materialKind != MaterialAssetKind::Instance) {
          continue;
        }
        if (!ContainsText(choice.label, query) &&
            !ContainsText(choice.guid.value, query)) {
          continue;
        }
        const bool selected =
            choice.guid == authoringDocument_.GetMaterialAssetGuid();
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

  const char *modelPreview = modelDisplayName_.empty()
                                 ? "Select a model..."
                                 : modelDisplayName_.c_str();
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##ModelMaterialModel", modelPreview)) {
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##ModelMaterialModelSearch", "Filter models...",
                             modelSearch_.data(), modelSearch_.size());
    ImGui::Separator();
    const std::string_view query(modelSearch_.data());
    for (const AssetChoice &choice : modelChoices_) {
      if (!ContainsText(choice.label, query) &&
          !ContainsText(choice.guid.value, query)) {
        continue;
      }
      ImGui::PushID(choice.guid.value.c_str());
      const std::string label =
          choice.label + (choice.ready ? "" : "  [import required]");
      if (ImGui::Selectable(label.c_str(), choice.guid == modelGuid_)) {
        (void)OpenModel(scene, choice.guid, statusMessage_);
      }
      ImGui::PopID();
    }
    if (modelChoices_.empty()) {
      ImGui::TextDisabled("No model assets are registered");
    }
    ImGui::EndCombo();
  }

  if (!IsEditingModel()) {
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Choose an imported model. The workspace consumes its HMODEL/HCMESH "
        "surface layout and never rewrites imported materials.");
    ImGui::End();
    return;
  }

  const AssetGuid activeSetGuid =
      authoringDocument_.GetBindingAssetGuid();
  const AssetChoice *activeSet = nullptr;
  for (const AssetChoice &choice : materialSetChoices_) {
    if (choice.guid == activeSetGuid) {
      activeSet = &choice;
      break;
    }
  }
  const char *setPreview = activeSet != nullptr
      ? activeSet->label.c_str()
      : "Unsaved Material Set";
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##ModelMaterialSet", setPreview)) {
    for (const AssetChoice &choice : materialSetChoices_) {
      if (choice.ownerModelGuid != modelGuid_) {
        continue;
      }
      if (ImGui::Selectable(
              choice.label.c_str(), choice.guid == activeSetGuid)) {
        (void)OpenMaterialSet(scene, choice.guid, statusMessage_);
      }
    }
    ImGui::Separator();
    if (ImGui::Selectable("+ New Material Set")) {
      (void)CreateMaterialSet(scene, statusMessage_);
    }
    ImGui::EndCombo();
  }
  ImGui::BeginDisabled(!authoringDocument_.IsBindingDirty());
  if (ImGui::Button("Save Set")) {
    (void)SaveBindings(scene, false, statusMessage_);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Save Set As...")) {
    ImGui::OpenPopup("SaveBindingAsPopupFromParts");
  }
  if (ImGui::BeginPopup("SaveBindingAsPopupFromParts")) {
    ImGui::TextUnformatted("Path under Assets");
    ImGui::SetNextItemWidth(300.0f);
    ImGui::InputText("##BindingSaveAsPath", bindingSaveAsPath_.data(),
                     bindingSaveAsPath_.size());
    if (ImGui::Button("Save Copy") &&
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
    ImGui::SetTooltip("Create another Material Set for this model");
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(
      !authoringDocument_.GetBindingAssetGuid().IsValid());
  if (ImGui::SmallButton("Trash##DeleteMaterialSet")) {
    ImGui::OpenPopup("DeleteCurrentMaterialSetPopup");
  }
  ImGui::EndDisabled();
  if (ImGui::BeginPopupModal(
          "DeleteCurrentMaterialSetPopup", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize)) {
    const AssetGuid setGuid =
        authoringDocument_.GetBindingAssetGuid();
    const AssetRecord* setRecord = setGuid.IsValid()
        ? scene.GetAssetDatabase().FindByGuid(setGuid)
        : nullptr;
    ImGui::TextUnformatted("Move this model Material Set to project trash?");
    if (setRecord != nullptr) {
      ImGui::TextWrapped("%s", setRecord->sourcePath.generic_string().c_str());
    }
    if (authoringDocument_.IsBindingDirty()) {
      ImGui::TextColored(
          ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
          "Unsaved set edits will be discarded.");
    }
    const AssetTrashReferenceAnalysis references =
        AnalyzeAssetTrashReferences(scene.GetAssetDatabase(), setGuid);
    if (references.IsReferenced()) {
      ImGui::SeparatorText("References");
      ImGui::TextColored(
          ImVec4(1.0f, 0.48f, 0.34f, 1.0f),
          "This Material Set is still selected by a Scene and cannot be deleted.");
      for (size_t index = 0u;
           index < (std::min)(references.references.size(), size_t{3u});
           ++index) {
        const AssetTrashReference& reference = references.references[index];
        ImGui::BulletText("%s (%s)",
            reference.ownerSourcePath.generic_string().c_str(),
            reference.role.c_str());
      }
    }
    ImGui::BeginDisabled(references.IsReferenced());
    if (ImGui::Button("Move to Trash", ImVec2(120.0f, 0.0f))) {
      const AssetTrashResult deleted = MoveAssetToProjectTrash(
          scene.GetAssetDatabase(), setGuid);
      statusMessage_ = deleted.message;
      if (deleted.success) {
        (void)scene.ReloadAssets();
        assetChoiceRevision_ = 0u;
        (void)LoadOrCreateBindings(scene, statusMessage_);
        RebuildSlotState(scene.GetAssetDatabase(), scene.GetAssetRegistry());
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::Text("%u material slots", stats_.materialSlotCount);
  ImGui::SameLine();
  if (authoringDocument_.IsBindingDirty()) {
    ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.3f, 1.0f), "Unsaved binding edits");
  }
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##ModelMaterialSlotSearch", "Filter name or slotId",
                           slotSearch_.data(), slotSearch_.size());
  ImGui::Separator();

  constexpr ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
      ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
      ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("##ModelMaterialSlots", 4, flags)) {
    ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 44.0f);
    ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 84.0f);
    ImGui::TableSetupColumn("Surfaces", ImGuiTableColumnFlags_WidthFixed,
                            62.0f);
    ImGui::TableHeadersRow();
    const std::string_view query(slotSearch_.data());
    for (const SlotState &slot : slots_) {
      const std::string slotId =
          ASSETS::MODELS::MaterialSlotIdToString(slot.view.slotId);
      if (!ContainsText(slot.view.displayName, query) &&
          !ContainsText(slotId, query)) {
        continue;
      }
      ImGui::PushID(static_cast<int>(slot.view.materialIndex));
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      const bool selected = selectedMaterialIndex_ &&
                            *selectedMaterialIndex_ == slot.view.materialIndex;
      const std::string id = std::to_string(slot.view.materialIndex) +
                             "##SelectMaterialSlot";
      if (ImGui::Selectable(id.c_str(), selected,
                            ImGuiSelectableFlags_SpanAllColumns |
                                ImGuiSelectableFlags_AllowOverlap)) {
        (void)SelectSlotForEditing(scene, slot.view.materialIndex,
                                   statusMessage_);
      }
      if (selected) {
        ImGui::GetWindowDrawList()->AddRect(
            ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
            IM_COL32(80, 220, 232, 255), 3.0f, 0, 2.0f);
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("Stable slotId: %s",
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
#else
  (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
