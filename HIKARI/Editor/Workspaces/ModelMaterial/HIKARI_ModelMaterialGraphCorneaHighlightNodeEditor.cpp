#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::
    DrawCorneaHighlightNodeInlineControls(
        DocumentSceneBase &scene, MaterialAssetData &source,
        MaterialGraphData &graph, float width) {
#if defined(HIKARI_WITH_EDITOR)
  bool capturesPointer = false;
  const auto trackItem = [&capturesPointer]() {
    capturesPointer = capturesPointer || ImGui::IsItemHovered() ||
                      ImGui::IsItemActive();
  };
  const auto commitContinuous = [this, &scene](
                                    MaterialAssetData before,
                                    std::string label, bool changed) {
    if (ImGui::IsItemActivated() && !materialGesture_) {
      materialGesture_ = MaterialEditGesture{std::move(before), label};
    }
    if (changed) {
      PreviewMaterialGesture(scene);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      CommitMaterialGesture(scene);
    }
  };
  const auto drawParameter = [&, this](std::string_view parameterId,
                                       const char *label) {
    MaterialGraphParameterData *parameter =
        MATERIAL_GRAPH_UI::FindParameter(graph, parameterId);
    if (parameter == nullptr) {
      ImGui::TextDisabled("%s: missing", label);
      return;
    }
    ImGui::PushID(parameter->id.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth((std::max)(
        72.0f, width - ImGui::CalcTextSize(label).x - 18.0f));
    MaterialAssetData before = source;
    const bool changed = MATERIAL_GRAPH_UI::DrawCompactMaterialValue(
        parameter->defaultValue, false);
    trackItem();
    commitContinuous(
        std::move(before), "Edit " + std::string(label), changed);
    ImGui::PopID();
  };

  ImGui::TextDisabled(
      "Uses this material's Base Color highlight artwork.");
  ImGui::TextDisabled(
      "Connect Color -> PBR Eye | Surface Output: Blend");
  MaterialGraphParameterData *mode = MATERIAL_GRAPH_UI::FindParameter(
      graph, "parameter.corneaHighlightMode");
  if (mode == nullptr) {
    ImGui::TextDisabled("Lighting Mode: missing");
    return capturesPointer;
  }

  int current = mode->defaultValue.numeric.x < 0.5f
      ? 0 : mode->defaultValue.numeric.x < 1.5f ? 1 : 2;
  constexpr const char *kModes[]{
      "Painted / Fixed",
      "Light Intensity",
      "Light Projection",
  };
  ImGui::SetNextItemWidth(width);
  if (ImGui::Combo("##CorneaHighlightMode", &current, kModes, 3)) {
    MaterialAssetData before = source;
    mode->defaultValue.numeric.x = static_cast<float>(current);
    CommitMaterialMutation(
        std::move(before), "Change cornea highlight mode", scene);
  }
  trackItem();

  if (current == 2) {
    drawParameter("parameter.corneaHighlightTravel", "Travel");
    drawParameter(
        "parameter.corneaHighlightViewInfluence", "View Influence");
    drawParameter(
        "parameter.corneaHighlightLightInfluence", "Light Influence");
    drawParameter(
        "parameter.corneaHighlightCurvature", "Cornea Curvature");
  }
  if (current >= 1) {
    drawParameter(
        "parameter.corneaHighlightMinBrightness", "Minimum Brightness");
    drawParameter(
        "parameter.corneaHighlightLightResponse", "Light Response");
    drawParameter(
        "parameter.corneaHighlightSharpness", "Response Sharpness");
    drawParameter(
        "parameter.corneaHighlightLightColor", "Follow Light Color");
  }
  drawParameter("parameter.corneaHighlightEdgeFade", "Iris Edge Fade");
  return capturesPointer;
#else
  (void)scene;
  (void)source;
  (void)graph;
  (void)width;
  return false;
#endif
}

} // namespace HIKARI::EDITOR
