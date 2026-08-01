#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "Assets/Models/HIKARI_ModelMaterialSlot.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
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
    return "Imported fallback";
  case ModelMaterialBindingSource::AuthoredOverride:
    return "Authored override";
  case ModelMaterialBindingSource::OverrideUnavailable:
    return "Override unavailable";
  }
  return "Unknown";
}

bool IsRenderableMaterialKind(MaterialAssetKind kind) noexcept {
  return kind == MaterialAssetKind::Material ||
         kind == MaterialAssetKind::Instance;
}

} // namespace

void ModelMaterialWorkspaceController::DrawBindingWindow(
    DocumentSceneBase &scene) {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Binding###ModelMaterial/Binding")) {
    ImGui::End();
    return;
  }
  const ModelMaterialSlotView *slot = GetSelectedSlot();
  if (slot == nullptr) {
    ImGui::TextWrapped("Select a material slot to edit its binding.");
    ImGui::End();
    return;
  }

  ImGui::TextUnformatted(slot->displayName.c_str());
  ImGui::SameLine();
  StatusBadge(BindingLabel(slot->source),
              slot->source == ModelMaterialBindingSource::AuthoredOverride
                  ? EditorStatusTone::Ready
              : slot->source ==
                        ModelMaterialBindingSource::OverrideUnavailable
                  ? EditorStatusTone::Warning
                  : EditorStatusTone::Normal);
  const std::string slotId =
      ASSETS::MODELS::MaterialSlotIdToString(slot->slotId);
  ImGui::TextDisabled("slotId %s | imported index %u",
                      slotId.empty() ? "missing" : slotId.c_str(),
                      slot->materialIndex);
  ImGui::SeparatorText("Material Asset");

  const AssetChoice *candidate = FindMaterialChoice(candidateMaterialGuid_);
  const bool candidateRenderable =
      candidate != nullptr && IsRenderableMaterialKind(candidate->materialKind);
  const char *candidateLabel = candidateRenderable
                                   ? candidate->label.c_str()
                               : candidateMaterialGuid_.IsValid()
                                   ? "Choose a renderable Material or Instance"
                                   : "Choose a material...";
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##BindingMaterialAsset", candidateLabel)) {
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##BindingMaterialSearch",
                             "Filter material assets...",
                             materialSearch_.data(), materialSearch_.size());
    ImGui::Separator();
    const std::string_view query(materialSearch_.data());
    for (const AssetChoice &choice : materialChoices_) {
      if (!IsRenderableMaterialKind(choice.materialKind)) {
        continue;
      }
      if (!ContainsText(choice.label, query) &&
          !ContainsText(choice.guid.value, query)) {
        continue;
      }
      ImGui::PushID(choice.guid.value.c_str());
      const std::string label =
          choice.label + "  [" + std::string(ToString(choice.materialKind)) +
          "]" + (choice.ready ? "" : "  [import required]");
      if (ImGui::Selectable(label.c_str(),
                            choice.guid == candidateMaterialGuid_)) {
        candidateMaterialGuid_ = choice.guid;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }

  const bool canBind = slot->slotId.IsValid() && candidateRenderable &&
                       candidate->ready;
  ImGui::BeginDisabled(!canBind);
  if (ImGui::Button("Bind Selected", ImVec2(-1.0f, 0.0f))) {
    ModelMaterialBindingCommand command{};
    command.type = ModelMaterialBindingCommandType::BindMaterialAsset;
    command.slotId = slot->slotId;
    command.materialIndexHint = slot->materialIndex;
    command.materialAssetGuid = candidateMaterialGuid_;
    statusMessage_ = ExecuteBindingCommand(
                         command, scene.GetAssetDatabase(),
                         scene.GetAssetRegistry())
                         .message;
    slot = GetSelectedSlot();
  }
  ImGui::EndDisabled();

  ImGui::BeginDisabled(!candidateRenderable);
  if (ImGui::Button("Open Selected Material")) {
    (void)OpenMaterialAsset(scene, candidateMaterialGuid_, statusMessage_);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(slot == nullptr ||
                       !slot->overrideMaterialGuid.IsValid());
  if (ImGui::Button("Restore Imported")) {
    ModelMaterialBindingCommand command{};
    command.type = ModelMaterialBindingCommandType::RestoreImportedFallback;
    command.slotId = slot->slotId;
    command.materialIndexHint = slot->materialIndex;
    statusMessage_ = ExecuteBindingCommand(
                         command, scene.GetAssetDatabase(),
                         scene.GetAssetRegistry())
                         .message;
  }
  ImGui::EndDisabled();

  ImGui::SeparatorText("Binding Document");
  const std::string path =
      authoringDocument_.GetBindingSourcePath().generic_string();
  ImGui::TextWrapped("%s", path.empty() ? "Unsaved binding asset"
                                        : path.c_str());
  StatusBadge(authoringDocument_.IsBindingDirty() ? "Dirty" : "Saved",
              authoringDocument_.IsBindingDirty() ? EditorStatusTone::Warning
                                                  : EditorStatusTone::Ready);
  ImGui::BeginDisabled(!authoringDocument_.IsBindingDirty());
  if (ImGui::Button("Save Bindings")) {
    (void)SaveBindings(scene, false, statusMessage_);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Save Bindings As...")) {
    ImGui::OpenPopup("SaveBindingAsPopup");
  }
  if (ImGui::BeginPopup("SaveBindingAsPopup")) {
    ImGui::TextUnformatted("Path under Assets");
    ImGui::SetNextItemWidth(340.0f);
    ImGui::InputText("##BindingSaveAsPath", bindingSaveAsPath_.data(),
                     bindingSaveAsPath_.size());
    if (ImGui::Button("Save Copy")) {
      if (SaveBindings(scene, true, statusMessage_)) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  if (slot != nullptr && !slot->diagnostic.empty()) {
    ImGui::TextColored(ImVec4(1.0f, 0.58f, 0.30f, 1.0f), "%s",
                       slot->diagnostic.c_str());
  }
  ImGui::TextDisabled(
      "Restore removes only this stable slotId override. The imported HMODEL "
      "material remains the fallback and is never rewritten.");
  ImGui::End();
#else
  (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
