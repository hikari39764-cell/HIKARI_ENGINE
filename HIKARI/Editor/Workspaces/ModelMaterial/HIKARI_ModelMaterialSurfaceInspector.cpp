#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

#if defined(HIKARI_WITH_EDITOR)
const char *AlphaModeLabel(MaterialAlphaMode mode) noexcept {
  switch (mode) {
  case MaterialAlphaMode::Opaque:
    return "Opaque";
  case MaterialAlphaMode::Mask:
    return "Cutout / Mask";
  case MaterialAlphaMode::Blend:
    return "Transparent / Blend";
  }
  return "Opaque";
}

void DrawHelp(const char *text) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
    ImGui::SetTooltip("%s", text);
  }
}
#endif

} // namespace

void ModelMaterialWorkspaceController::DrawMaterialSurfaceControls(
    DocumentSceneBase &scene, MaterialAssetData &source) {
#if defined(HIKARI_WITH_EDITOR)
  if (source.kind != MaterialAssetKind::Material) {
    return;
  }

  PbrMaterialAssetData pbr = ProjectPbrMaterialAssetData(source);
  const auto finishContinuous =
      [&](MaterialAssetData before, bool changed, const char *label) {
        if (ImGui::IsItemActivated()) {
          BeginMaterialGesture(label);
        }
        if (changed) {
          ApplyPbrMaterialAssetData(source, pbr);
          if (materialGesture_) {
            PreviewMaterialGesture(scene);
          } else {
            CommitMaterialMutation(std::move(before), label, scene);
          }
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
          CommitMaterialGesture(scene);
        }
      };
  const auto commitPbr = [&](MaterialAssetData before,
                             const char *label) {
    ApplyPbrMaterialAssetData(source, pbr);
    CommitMaterialMutation(std::move(before), label, scene);
  };
  const auto drawTexture = [&](const char *label,
                               MaterialTextureSlotData &slot,
                               const char *helpText) {
    ImGui::PushID(label);
    bool enabled = slot.useTexture;
    MaterialAssetData before = source;
    if (ImGui::Checkbox("Use texture", &enabled)) {
      slot.useTexture = enabled;
      if (!slot.textureAssetGuid.IsValid()) {
        slot.useTexture = false;
      }
      commitPbr(std::move(before), "Toggle material texture");
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(label);
    DrawHelp(helpText);

    std::string textureGuid = slot.textureAssetGuid.value;
    AssetFieldOptions options{};
    options.label = label;
    options.requiredType = AssetType::Texture;
    options.drawLabel = false;
    before = source;
    if (DrawAssetField(&scene.GetAssetDatabase(), options, textureGuid)) {
      slot.textureAssetGuid = AssetGuid{textureGuid};
      slot.useTexture = slot.textureAssetGuid.IsValid();
      commitPbr(std::move(before), "Choose material texture");
    }

    if (ImGui::TreeNodeEx("Advanced Mapping",
                          ImGuiTreeNodeFlags_SpanAvailWidth)) {
      int uvSet = static_cast<int>(slot.texCoord);
      before = source;
      if (ImGui::Combo("UV Map", &uvSet, "UV0\0UV1\0")) {
        slot.texCoord = uvSet;
        commitPbr(std::move(before), "Change texture UV map");
      }
      before = source;
      const bool scaleChanged =
          ImGui::DragFloat2("Scale", &slot.uvScale.x, 0.01f);
      finishContinuous(std::move(before), scaleChanged,
                       "Edit texture UV scale");
      before = source;
      const bool offsetChanged =
          ImGui::DragFloat2("Offset", &slot.uvOffset.x, 0.01f);
      finishContinuous(std::move(before), offsetChanged,
                       "Edit texture UV offset");
      before = source;
      const bool rotationChanged =
          ImGui::DragFloat("Rotation", &slot.uvRotation, 0.01f);
      finishContinuous(std::move(before), rotationChanged,
                       "Edit texture UV rotation");
      ImGui::TreePop();
    }
    ImGui::PopID();
  };

  ImGui::SeparatorText("Surface");
  ImGui::TextDisabled(
      "These controls edit the connected graph parameters and refresh the "
      "runtime material preview live.");

  ImGui::TextUnformatted("Base Color");
  ImGui::TextDisabled("Texture x Tint");
  {
    MaterialAssetData before = source;
    const bool changed = ImGui::ColorEdit4(
        "Tint", &pbr.baseColorFactor.x,
        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
    DrawHelp("Final Base Color is the sampled texture multiplied by this tint.");
    finishContinuous(std::move(before), changed, "Edit base color tint");
  }
  drawTexture("Base Color Texture", pbr.baseColorTexture,
              "Choose the texture sampled for the material Base Color.");

  ImGui::Spacing();
  ImGui::TextUnformatted("Material Response");
  {
    MaterialAssetData before = source;
    const bool changed = ImGui::SliderFloat(
        "Metallic", &pbr.metallicFactor, 0.0f, 1.0f, "%.3f");
    DrawHelp("0 is dielectric such as skin or cloth; 1 is a metal surface.");
    finishContinuous(std::move(before), changed, "Edit metallic");
  }
  {
    MaterialAssetData before = source;
    const bool changed = ImGui::SliderFloat(
        "Roughness", &pbr.roughnessFactor, 0.02f, 1.0f, "%.3f");
    DrawHelp("Lower values produce a sharper highlight; higher values spread it.");
    finishContinuous(std::move(before), changed, "Edit roughness");
  }
  drawTexture("Metallic / Roughness Texture",
              pbr.metallicRoughnessTexture,
              "Uses the imported metallic-roughness channel convention.");

  ImGui::Spacing();
  ImGui::TextUnformatted("Normal");
  {
    MaterialAssetData before = source;
    const bool changed = ImGui::SliderFloat(
        "Strength", &pbr.normalScale, 0.0f, 2.0f, "%.3f");
    finishContinuous(std::move(before), changed, "Edit normal strength");
  }
  drawTexture("Normal Texture", pbr.normalTexture,
              "Choose a tangent-space normal texture.");

  ImGui::Spacing();
  ImGui::TextUnformatted("Emission");
  {
    MaterialAssetData before = source;
    const bool changed =
        ImGui::ColorEdit3("Color", &pbr.emissiveFactor.x,
                          ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    finishContinuous(std::move(before), changed, "Edit emission color");
  }
  {
    MaterialAssetData before = source;
    const bool changed = ImGui::DragFloat(
        "Intensity", &pbr.emissiveStrength, 0.05f, 0.0f, 128.0f);
    finishContinuous(std::move(before), changed, "Edit emission intensity");
  }
  drawTexture("Emission Texture", pbr.emissiveTexture,
              "The sampled emission texture is multiplied by Color and Intensity.");

  ImGui::SeparatorText("Rendering");
  {
    MaterialAssetData before = source;
    if (ImGui::Checkbox("Double-sided", &pbr.doubleSided)) {
      commitPbr(std::move(before), "Toggle double-sided");
    }
  }
  ImGui::SameLine();
  {
    MaterialAssetData before = source;
    if (ImGui::Checkbox("Unlit", &pbr.unlit)) {
      commitPbr(std::move(before), "Toggle unlit");
    }
  }

  if (ImGui::BeginCombo("Alpha Mode", AlphaModeLabel(source.surface.alphaMode))) {
    for (MaterialAlphaMode mode : {MaterialAlphaMode::Opaque,
                                   MaterialAlphaMode::Mask,
                                   MaterialAlphaMode::Blend}) {
      if (ImGui::Selectable(AlphaModeLabel(mode),
                            source.surface.alphaMode == mode)) {
        MaterialAssetData before = source;
        source.surface.alphaMode = mode;
        CommitMaterialMutation(std::move(before), "Change alpha mode", scene);
      }
    }
    ImGui::EndCombo();
  }
  if (source.surface.alphaMode == MaterialAlphaMode::Mask) {
    MaterialAssetData before = source;
    float alphaCutoff = source.surface.alphaCutoff;
    const bool changed = ImGui::SliderFloat(
        "Cutout Threshold", &alphaCutoff, 0.0f, 1.0f,
        "%.3f");
    if (ImGui::IsItemActivated()) {
      BeginMaterialGesture("Edit cutout threshold");
    }
    if (changed) {
      source.surface.alphaCutoff = alphaCutoff;
      if (materialGesture_) {
        PreviewMaterialGesture(scene);
      } else {
        CommitMaterialMutation(std::move(before),
                               "Edit cutout threshold", scene);
      }
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      CommitMaterialGesture(scene);
    }
  }
#else
  (void)scene;
  (void)source;
#endif
}

} // namespace HIKARI::EDITOR
