#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::CreateRampPresetAndBind(
    DocumentSceneBase &scene, MaterialAssetData &source,
    MaterialGraphData &graph, MaterialGraphNodeData &node,
    ASSETS::MATERIAL::MaterialRampPresetId presetId,
    std::string &outMessage) {
  MaterialGraphResourceData *resource =
      MATERIAL_GRAPH_UI::FindRampResource(graph, node);
  MaterialAssetData before = source;

  const auto &presets = ASSETS::MATERIAL::GetBuiltInMaterialRampPresets();
  const auto info = std::find_if(
      presets.begin(), presets.end(), [presetId](const auto &preset) {
        return preset.id == presetId;
      });
  const std::string copyName = source.materialName + " " +
      std::string(info != presets.end() ? info->displayName : "Ramp");
  ASSETS::MATERIAL::MaterialAssetAuthoringService service(
      scene.GetAssetDatabase());
  const ASSETS::MATERIAL::MaterialAuthoringResult created = service.Create(
      ASSETS::MATERIAL::CreateMaterialRampFromPreset(presetId, copyName),
      "Materials/Ramps");
  if (!created.success) {
    outMessage = created.message;
    return false;
  }
  if (!scene.ReloadAssets()) {
    outMessage =
        "Ramp asset was created, but Registry publication failed";
    return false;
  }
  if (resource == nullptr) {
    MaterialGraphResourceData createdResource{};
    createdResource.id =
        "Ramp_" + std::to_string(authoringIdCounter_++);
    createdResource.displayName = "Diffuse Ramp";
    createdResource.type = MaterialResourceType::Ramp1D;
    createdResource.semantic = MaterialResourceSemantic::Ramp;
    graph.resources.push_back(std::move(createdResource));
    node.referenceId = graph.resources.back().id;
    resource = &graph.resources.back();
  }
  resource->assetGuid = created.assetGuid;
  resource->enabled = true;
  CommitMaterialMutation(std::move(before), "Create and bind Ramp asset",
                         scene);
  assetChoiceRevision_ = 0u;
  RefreshAssetChoices(scene.GetAssetDatabase());
  outMessage = "created an editable Ramp copy and bound it to the node";
  (void)BeginInlineRampEdit(scene, created.assetGuid, outMessage);
  return true;
}

bool ModelMaterialWorkspaceController::BeginInlineRampEdit(
    DocumentSceneBase &scene, const AssetGuid &rampGuid,
    std::string &outMessage) {
  ASSETS::MATERIAL::MaterialAssetAuthoringService service(
      scene.GetAssetDatabase());
  ASSETS::MATERIAL::MaterialAuthoringDocument loaded{};
  if (!service.Load(rampGuid, loaded, outMessage) ||
      loaded.source.kind != MaterialAssetKind::Ramp) {
    if (outMessage.empty()) {
      outMessage = "selected asset is not a Ramp";
    }
    return false;
  }
  inlineRampDocument_.OpenMaterial(
      loaded.assetGuid, loaded.sourcePath, std::move(loaded.source),
      loaded.compile, true);
  inlineRampPointId_.clear();
  if (!inlineRampDocument_.GetMaterial().ramp.points.empty()) {
    inlineRampPointId_ =
        inlineRampDocument_.GetMaterial().ramp.points.front().id;
  }
  inlineRampGestureBefore_.reset();
  inlineRampGestureLabel_.clear();
  inlineRampPopupRequested_ = true;
  outMessage = "Ramp opened from the graph node";
  return true;
}

void ModelMaterialWorkspaceController::DrawInlineRampEditorPopup(
    DocumentSceneBase &scene) {
#if defined(HIKARI_WITH_EDITOR)
  if (inlineRampPopupRequested_) {
    ImGui::OpenPopup("Ramp Editor###ModelMaterial/InlineRampEditor");
    inlineRampPopupRequested_ = false;
  }
  if (!ImGui::BeginPopupModal(
          "Ramp Editor###ModelMaterial/InlineRampEditor", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  if (!inlineRampDocument_.HasMaterial()) {
    ImGui::TextDisabled("No Ramp is open.");
    if (ImGui::Button("Close")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return;
  }

  MaterialAssetData &source = inlineRampDocument_.EditMaterial();
  MaterialRampData &ramp = source.ramp;
  ImGui::Text("%s%s", source.materialName.c_str(),
              inlineRampDocument_.IsMaterialDirty() ? " *" : "");
  ImGui::TextDisabled(
      "This Ramp is a reusable project asset. Save updates every material "
      "that references it.");
  ImGui::BeginDisabled(!inlineRampDocument_.CanUndo());
  if (ImGui::Button("Undo")) {
    ModelMaterialWorkspaceDocument::ChangedDomain domain{};
    std::string label{};
    (void)inlineRampDocument_.Undo(domain, label);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!inlineRampDocument_.CanRedo());
  if (ImGui::Button("Redo")) {
    ModelMaterialWorkspaceDocument::ChangedDomain domain{};
    std::string label{};
    (void)inlineRampDocument_.Redo(domain, label);
  }
  ImGui::EndDisabled();

  if (ImGui::BeginCombo("Interpolation",
                        ToString(ramp.interpolation).data())) {
    for (MaterialRampInterpolation value : {
             MaterialRampInterpolation::Constant,
             MaterialRampInterpolation::Linear,
             MaterialRampInterpolation::Smooth}) {
      if (ImGui::Selectable(ToString(value).data(),
                            value == ramp.interpolation)) {
        MaterialAssetData before = source;
        ramp.interpolation = value;
        (void)inlineRampDocument_.CommitMaterial(
            std::move(before), "Change Ramp interpolation");
      }
    }
    ImGui::EndCombo();
  }

  ImGui::SeparatorText("Gradient");
  MATERIAL_GRAPH_UI::DrawRampStrip(ramp, 520.0f, 72.0f);
  const ImVec2 stripMin = ImGui::GetItemRectMin();
  const ImVec2 stripMax = ImGui::GetItemRectMax();
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  for (const MaterialRampPointData &point : ramp.points) {
    const ImVec2 marker{
        stripMin.x + std::clamp(point.position.x, 0.0f, 1.0f) *
                         (stripMax.x - stripMin.x),
        stripMax.y - 7.0f};
    const bool selected = inlineRampPointId_ == point.id;
    drawList->AddTriangleFilled(
        {marker.x, marker.y - 9.0f}, {marker.x - 7.0f, marker.y + 5.0f},
        {marker.x + 7.0f, marker.y + 5.0f},
        selected ? IM_COL32(255, 188, 72, 255)
                 : IM_COL32(236, 241, 246, 255));
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
        std::abs(mouse.x - marker.x) <= 10.0f) {
      inlineRampPointId_ = point.id;
    }
  }

  if (ImGui::Button("Add Point")) {
    MaterialAssetData before = source;
    MaterialRampPointData point{};
    point.id = "RampPoint_" + std::to_string(authoringIdCounter_++);
    point.position = {0.5f, 0.0f};
    point.color = {0.7f, 0.75f, 0.85f, 1.0f};
    inlineRampPointId_ = point.id;
    ramp.points.push_back(std::move(point));
    (void)inlineRampDocument_.CommitMaterial(std::move(before),
                                             "Add Ramp point");
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(ramp.points.size() <= 2u ||
                       inlineRampPointId_.empty());
  if (ImGui::Button("Delete Point")) {
    MaterialAssetData before = source;
    std::erase_if(ramp.points, [this](const auto &point) {
      return point.id == inlineRampPointId_;
    });
    inlineRampPointId_ = ramp.points.empty() ? std::string{}
                                              : ramp.points.front().id;
    (void)inlineRampDocument_.CommitMaterial(std::move(before),
                                             "Delete Ramp point");
  }
  ImGui::EndDisabled();

  const auto selected = std::find_if(
      ramp.points.begin(), ramp.points.end(), [this](const auto &point) {
        return point.id == inlineRampPointId_;
      });
  if (selected != ramp.points.end()) {
    ImGui::SeparatorText("Selected Point");
    MaterialAssetData beforePosition = source;
    const bool moved = ImGui::SliderFloat(
        "Position", &selected->position.x, 0.0f, 1.0f);
    if (ImGui::IsItemActivated() && !inlineRampGestureBefore_) {
      inlineRampGestureBefore_ = std::move(beforePosition);
      inlineRampGestureLabel_ = "Move Ramp point";
    }
    if (moved) {
      selected->position.y = 0.0f;
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && inlineRampGestureBefore_) {
      (void)inlineRampDocument_.CommitMaterial(
          std::move(*inlineRampGestureBefore_), inlineRampGestureLabel_);
      inlineRampGestureBefore_.reset();
    }
    MaterialAssetData beforeColor = source;
    (void)ImGui::ColorEdit4("Color", &selected->color.x,
                            ImGuiColorEditFlags_Float);
    if (ImGui::IsItemActivated() && !inlineRampGestureBefore_) {
      inlineRampGestureBefore_ = std::move(beforeColor);
      inlineRampGestureLabel_ = "Edit Ramp color";
    }
    if (ImGui::IsItemDeactivatedAfterEdit() && inlineRampGestureBefore_) {
      (void)inlineRampDocument_.CommitMaterial(
          std::move(*inlineRampGestureBefore_), inlineRampGestureLabel_);
      inlineRampGestureBefore_.reset();
    }
  }

  ImGui::Separator();
  ImGui::BeginDisabled(!inlineRampDocument_.IsMaterialDirty());
  if (ImGui::Button("Save Ramp", {120.0f, 0.0f})) {
    ASSETS::MATERIAL::MaterialAssetAuthoringService service(
        scene.GetAssetDatabase());
    const ASSETS::MATERIAL::MaterialAuthoringResult saved = service.Update(
        inlineRampDocument_.GetMaterialAssetGuid(), source);
    statusMessage_ = saved.message;
    if (saved.success) {
      if (!scene.ReloadAssets()) {
        statusMessage_ += "; Registry publication failed";
      } else {
        inlineRampDocument_.MarkMaterialSaved(saved.assetGuid,
                                               saved.sourcePath);
        assetChoiceRevision_ = 0u;
        RefreshAssetChoices(scene.GetAssetDatabase());
        (void)RefreshMaterialPreviews(scene, statusMessage_);
        inlineRampDocument_.CloseMaterial();
        ImGui::CloseCurrentPopup();
      }
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Cancel", {100.0f, 0.0f})) {
    inlineRampDocument_.CloseMaterial();
    inlineRampGestureBefore_.reset();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
#else
  (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
