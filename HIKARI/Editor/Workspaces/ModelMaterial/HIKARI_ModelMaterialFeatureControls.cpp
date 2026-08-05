#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>

#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::DrawMaterialFeatureControls(
    DocumentSceneBase &scene, MaterialAssetData &source) {
#if defined(HIKARI_WITH_EDITOR)
  if (source.kind != MaterialAssetKind::Material) {
    return;
  }

  ImGui::SeparatorText("Quick PBR / NPR Compare");
  const auto previewTemplate = [this, &scene, &source](
                                   MaterialTemplateId templateId) {
    MaterialAssetData candidate =
        ASSETS::MATERIAL::CreateMaterialFromTemplate(
            templateId, source.materialName);
    candidate.pbr = source.pbr;
    candidate.surface = source.surface;
    if (authoringDocument_.BeginAbPreview(
            std::move(candidate), statusMessage_)) {
      (void)RefreshMaterialPreviews(scene, statusMessage_);
    }
  };
  ImGui::BeginDisabled(authoringDocument_.IsAbPreviewActive());
  if (ImGui::Button("Preview PBR", ImVec2(-1.0f, 0.0f))) {
    previewTemplate(MaterialTemplateId::StandardPbr);
  }
  if (ImGui::Button("Preview Generic NPR", ImVec2(-1.0f, 0.0f))) {
    previewTemplate(MaterialTemplateId::GenericToon);
  }
  ImGui::EndDisabled();
  ImGui::TextDisabled(
      "The comparison preserves this material's PBR texture bindings and "
      "surface state. Keep B commits one Undo step; Discard restores A.");

  ImGui::SeparatorText("Render Features");
  for (MaterialFeatureRequest feature : {
           MaterialFeatureRequest::VisibilityOverlay,
           MaterialFeatureRequest::FaceHairShadow}) {
    const auto found = std::find(source.featureRequests.begin(),
                                 source.featureRequests.end(), feature);
    bool enabled = found != source.featureRequests.end();
    if (ImGui::Checkbox(ToString(feature).data(), &enabled)) {
      MaterialAssetData before = source;
      if (enabled) {
        source.featureRequests.push_back(feature);
      } else if (found != source.featureRequests.end()) {
        source.featureRequests.erase(found);
      }
      CommitMaterialMutation(std::move(before),
                             "Change material render feature", scene);
    }
  }
  ImGui::TextDisabled(
      "Outline is authored by connecting an Outline node to PBR Surface. "
      "View overlays remain explicit runtime features here.");
#else
  (void)scene;
  (void)source;
#endif
}

void ModelMaterialWorkspaceController::DrawOutlineThicknessPresets(
    DocumentSceneBase &scene, MaterialAssetData &source,
    MaterialGraphData &graph) {
#if defined(HIKARI_WITH_EDITOR)
  MaterialGraphParameterData *outlineThickness =
      MATERIAL_GRAPH_UI::FindParameter(
          graph, "parameter.outlineThickness");
  if (outlineThickness == nullptr) {
    return;
  }
  ImGui::TextDisabled("Outline Thickness Presets");
  const auto applyThickness = [&](float thickness, const char *label) {
    if (ImGui::Button(label)) {
      MaterialAssetData before = source;
      outlineThickness->defaultValue.numeric.x = thickness;
      CommitMaterialMutation(
          std::move(before),
          "Apply outline thickness preset", scene);
    }
  };
  applyThickness(1.0f, "Thin");
  ImGui::SameLine();
  applyThickness(2.0f, "Medium");
  ImGui::SameLine();
  applyThickness(4.0f, "Thick");
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Geometry outline thickness is measured in screen pixels and "
        "clamped to 0.25..8. Screen mode is view-space.");
  }
#else
  (void)scene;
  (void)source;
  (void)graph;
#endif
}

} // namespace HIKARI::EDITOR
