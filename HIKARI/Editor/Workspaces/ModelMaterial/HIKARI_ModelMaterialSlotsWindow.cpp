#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Models/HIKARI_ModelMaterialSlot.h"
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
  if (!ImGui::Begin("Models & Material Slots###ModelMaterial/Slots")) {
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
        (void)SelectSlot(slot.view.materialIndex);
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
