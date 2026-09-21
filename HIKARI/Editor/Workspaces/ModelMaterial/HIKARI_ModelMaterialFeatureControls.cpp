#include "Core/Text/HIKARI_Localization.h"
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

  ImGui::SeparatorText(TEXT::Localize("editor.ui.quick.pbr.npr.compare.c759fa").c_str());
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
  if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.preview.pbr.2029e1", "Preview PBR").c_str(), ImVec2(-1.0f, 0.0f))) {
    previewTemplate(MaterialTemplateId::StandardPbr);
  }
  if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.preview.generic.npr.177b33", "Preview Generic NPR").c_str(), ImVec2(-1.0f, 0.0f))) {
    previewTemplate(MaterialTemplateId::GenericToon);
  }
  ImGui::EndDisabled();
  ImGui::TextDisabled(TEXT::Localize("editor.ui.the.comparison.preserves.this.material.s.pbr.texture.bindings.an.6e8a98").c_str());

  ImGui::SeparatorText(TEXT::Localize("editor.ui.render.features.a321bd").c_str());
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
  ImGui::TextDisabled(TEXT::Localize("editor.ui.outline.is.authored.by.connecting.an.outline.node.to.pbr.surface.866199").c_str());
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
  ImGui::TextDisabled(TEXT::Localize("editor.ui.outline.thickness.presets.a424d6").c_str());
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
    ImGui::SetTooltip(TEXT::Localize("editor.ui.outline.thickness.is.measured.in.screen.pixels.and.clamped.to.0..2901c8").c_str());
  }
#else
  (void)scene;
  (void)source;
  (void)graph;
#endif
}

} // namespace HIKARI::EDITOR
