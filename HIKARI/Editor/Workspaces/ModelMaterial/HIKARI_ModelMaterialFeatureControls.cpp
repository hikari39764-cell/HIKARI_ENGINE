#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>

#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
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
      (void)RebuildAuthoredPreview(scene.GetAssetDatabase(),
                                   scene.GetAssetRegistry(), statusMessage_);
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
           MaterialFeatureRequest::GeometryOutline,
           MaterialFeatureRequest::ScreenOutline,
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
      "Features are planned per view. Disabled or missing inputs allocate no "
      "pass or target; isolated post groups never fall back globally.");
#else
  (void)scene;
  (void)source;
#endif
}

} // namespace HIKARI::EDITOR
