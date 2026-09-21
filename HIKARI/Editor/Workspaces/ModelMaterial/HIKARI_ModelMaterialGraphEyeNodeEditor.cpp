#include "Core/Text/HIKARI_Localization.h"
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

bool ModelMaterialWorkspaceController::DrawEyeLightingNodeInlineControls(
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
    if (ImGui::IsItemDeactivated()) {
      CommitMaterialGesture(scene);
    }
  };
  const auto drawParameter = [&, this](
                                 std::string_view parameterId,
                                 const char *label,
                                 MaterialValueType type,
                                 const MATH::Vec4 &runtimeDefault) {
    MaterialGraphParameterData *parameter =
        MATERIAL_GRAPH_UI::FindParameter(graph, parameterId);
    ImGui::PushID(
        parameterId.data(), parameterId.data() + parameterId.size());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    ImGui::SetNextItemWidth((std::max)(
        72.0f, width - ImGui::CalcTextSize(label).x - 18.0f));

    MaterialValueData runtimeValue{};
    runtimeValue.type = type;
    runtimeValue.numeric = runtimeDefault;
    MaterialValueData &editable = parameter != nullptr
        ? parameter->defaultValue
        : runtimeValue;
    MaterialAssetData before = source;
    const bool changed = MATERIAL_GRAPH_UI::DrawCompactMaterialValue(
        editable, false);
    trackItem();
    if (changed && parameter == nullptr) {
      MaterialGraphParameterData authored{};
      authored.id = std::string(parameterId);
      authored.displayName = label;
      authored.defaultValue = runtimeValue;
      authored.exposed = true;
      graph.parameters.push_back(std::move(authored));
    }
    commitContinuous(
        std::move(before), "Edit " + std::string(label), changed);
    if (parameter == nullptr && !changed && ImGui::IsItemHovered()) {
      ImGui::SetTooltip(TEXT::Localize("editor.ui.runtime.default.editing.creates.the.authored.parameter.c91fef").c_str());
    }
    ImGui::PopID();
  };

  ImGui::TextDisabled(TEXT::Localize("editor.ui.optional.eye.control.texture.sample.color.control.5dc66f").c_str());
  ImGui::TextDisabled(TEXT::Localize("editor.ui.r.iris.g.cornea.b.catchlight.a.parallax.mask.linear.ec9a4c").c_str());
  ImGui::TextDisabled(TEXT::Localize("editor.ui.no.control.map.uses.the.editable.radial.iris.mask.below.7dd4cd").c_str());

  MaterialGraphParameterData *eyeLayerMode =
      MATERIAL_GRAPH_UI::FindParameter(graph, "parameter.eyeLayerMode");
  int layerMode = eyeLayerMode != nullptr &&
          eyeLayerMode->defaultValue.numeric.x >= 0.5f
      ? 1
      : 0;
  const char *kEyeLayerModes[]{
      TEXT::LocalizeCStr("editor.ui.iris.base.38a41d"),
      TEXT::LocalizeCStr("editor.ui.transparent.highlight.7964bc"),
  };
  ImGui::SetNextItemWidth(width);
  if (ImGui::Combo("##EyeLayerMode", &layerMode, kEyeLayerModes, 2)) {
    MaterialAssetData before = source;
    if (eyeLayerMode == nullptr) {
      MaterialGraphParameterData parameter{};
      parameter.id = "parameter.eyeLayerMode";
      parameter.displayName = "Eye Layer Mode";
      parameter.defaultValue.type = MaterialValueType::Float1;
      parameter.defaultValue.numeric.x = static_cast<float>(layerMode);
      parameter.exposed = true;
      graph.parameters.push_back(std::move(parameter));
    } else {
      eyeLayerMode->defaultValue.numeric.x = static_cast<float>(layerMode);
    }
    if (layerMode == 1) {
      source.surface.alphaMode = MaterialAlphaMode::Blend;
      source.surface.alphaRepresentation =
          MaterialAlphaRepresentation::Straight;
    }
    CommitMaterialMutation(
        std::move(before), "Change eye layer mode", scene);
  }
  trackItem();

  drawParameter(
      "parameter.irisDepth", "Iris Depth", MaterialValueType::Float1,
      {0.12f, 0.0f, 0.0f, 0.0f});
  drawParameter(
      "parameter.eyeParallaxScale", "Parallax Scale",
      MaterialValueType::Float1, {0.10f, 0.0f, 0.0f, 0.0f});
  drawParameter(
      "parameter.irisCenter", "Iris UV Center", MaterialValueType::Float2,
      {0.50f, 0.50f, 0.0f, 0.0f});
  drawParameter(
      "parameter.irisRadius", "Iris UV Radius", MaterialValueType::Float1,
      {0.48f, 0.0f, 0.0f, 0.0f});
  drawParameter(
      "parameter.irisMaskSoftness", "Mask Softness",
      MaterialValueType::Float1, {0.08f, 0.0f, 0.0f, 0.0f});

  if (layerMode == 1) {
    ImGui::TextDisabled(TEXT::Localize("editor.ui.use.surface.output.blend.straight.for.png.art.efb36d").c_str());
    drawParameter(
        "parameter.eyeHighlightOpacity", "Highlight Opacity",
        MaterialValueType::Float1, {1.0f, 0.0f, 0.0f, 0.0f});
    drawParameter(
        "parameter.eyeHighlightThreshold", "Black Threshold",
        MaterialValueType::Float1, {0.02f, 0.0f, 0.0f, 0.0f});
    drawParameter(
        "parameter.eyeHighlightSoftness", "Highlight Softness",
        MaterialValueType::Float1, {0.04f, 0.0f, 0.0f, 0.0f});
    drawParameter(
        "parameter.eyeHighlightIntensity", "HDR Intensity",
        MaterialValueType::Float1, {2.0f, 0.0f, 0.0f, 0.0f});
  } else {
    drawParameter(
        "parameter.eyeSparkle", "Catchlight Intensity",
        MaterialValueType::Float1, {0.35f, 0.0f, 0.0f, 0.0f});
    drawParameter(
        "parameter.eyeSparklePower", "Catchlight Sharpness",
        MaterialValueType::Float1, {64.0f, 0.0f, 0.0f, 0.0f});
    drawParameter(
        "parameter.eyeBaseLift", "Iris Light Lift",
        MaterialValueType::Float1, {0.18f, 0.0f, 0.0f, 0.0f});
    drawParameter(
        "parameter.eyeCausticIntensity", "Caustic Intensity",
        MaterialValueType::Float1, {0.25f, 0.0f, 0.0f, 0.0f});
    drawParameter(
        "parameter.eyeCausticTint", "Caustic Tint",
        MaterialValueType::Float3, {1.0f, 0.72f, 0.38f, 0.0f});
    drawParameter(
        "parameter.eyeCausticWidth", "Caustic Width",
        MaterialValueType::Float1, {0.28f, 0.0f, 0.0f, 0.0f});
  }
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
