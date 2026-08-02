#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphDefaultInputs.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

#if defined(HIKARI_WITH_EDITOR)
bool DrawCompactValue(MaterialValueData &value, bool color) {
  switch (value.type) {
  case MaterialValueType::Bool:
    return ImGui::Checkbox("##Value", &value.boolean);
  case MaterialValueType::Float1:
    return ImGui::DragFloat("##Value", &value.numeric.x, 0.01f);
  case MaterialValueType::Float2:
    return ImGui::DragFloat2("##Value", &value.numeric.x, 0.01f);
  case MaterialValueType::Float3:
    return color
        ? ImGui::ColorEdit3("##Value", &value.numeric.x,
                            ImGuiColorEditFlags_Float |
                            ImGuiColorEditFlags_NoInputs |
                            ImGuiColorEditFlags_NoLabel)
        : ImGui::DragFloat3("##Value", &value.numeric.x, 0.01f);
  case MaterialValueType::Float4:
    return color
        ? ImGui::ColorEdit4("##Value", &value.numeric.x,
                            ImGuiColorEditFlags_Float |
                            ImGuiColorEditFlags_NoInputs |
                            ImGuiColorEditFlags_NoLabel)
        : ImGui::DragFloat4("##Value", &value.numeric.x, 0.01f);
  default:
    ImGui::TextDisabled("%s", ToString(value.type).data());
    return false;
  }
}
#endif

} // namespace

bool ModelMaterialWorkspaceController::DrawMaterialGraphNodeInlineControls(
    DocumentSceneBase &scene, MaterialAssetData &source,
    MaterialGraphData &graph, MaterialGraphNodeData &node,
    float x, float y, float width) {
#if defined(HIKARI_WITH_EDITOR)
  const ImVec2 restore = ImGui::GetCursorScreenPos();
  bool capturesPointer = false;
  ImGui::PushID(node.id.c_str());
  ImGui::SetCursorScreenPos({x, y});
  ImGui::PushClipRect(
      {x, y},
      {x + width,
       y + MATERIAL_GRAPH_UI::NodeInlineHeight(node.kind) - 5.0f},
      true);
  ImGui::BeginGroup();
  ImGui::PushItemWidth(width);

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
  const auto drawParameter = [&, this](
                                 std::string_view parameterId,
                                 const char *label,
                                 std::string_view inputPin = {}) {
    MaterialGraphParameterData *parameter =
        MATERIAL_GRAPH_UI::FindParameter(graph, parameterId);
    if (parameter == nullptr) {
      ImGui::TextDisabled("%s: missing", label);
      return;
    }
    const bool controlledByDefault = inputPin.empty() ||
        MATERIAL_GRAPH_UI::InputUsesParameterDefault(
            graph, node, inputPin, parameterId);
    ImGui::PushID(parameter->id.c_str());
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
    if (!controlledByDefault) {
      ImGui::TextDisabled("Linked");
      ImGui::PopID();
      return;
    }
    ImGui::SetNextItemWidth((std::max)(72.0f,
        width - ImGui::CalcTextSize(label).x - 18.0f));
    MaterialAssetData before = source;
    const bool changed = DrawCompactValue(
        parameter->defaultValue,
        MATERIAL_GRAPH_UI::LooksLikeColor(
            parameter->displayName, parameter->id));
    trackItem();
    if (parameter->defaultValue.type == MaterialValueType::Bool) {
      if (changed) {
        CommitMaterialMutation(
            std::move(before), "Edit " + parameter->displayName, scene);
      }
    } else {
      commitContinuous(
          std::move(before), "Edit " + parameter->displayName, changed);
    }
    ImGui::PopID();
  };
  const auto drawTexture = [&, this](
                               std::string_view resourceId,
                               const char *label,
                               std::string_view inputPin = {}) {
    MaterialGraphResourceData *resource =
        MATERIAL_GRAPH_UI::FindGraphResource(graph, resourceId);
    if (resource == nullptr) {
      ImGui::TextDisabled("%s: missing", label);
      return;
    }
    const bool controlledByDefault = inputPin.empty() ||
        MATERIAL_GRAPH_UI::InputUsesResourceDefault(
            graph, node, inputPin, resourceId);
    ImGui::PushID(resource->id.c_str());
    ImGui::TextDisabled("%s", label);
    if (!controlledByDefault) {
      ImGui::SameLine();
      ImGui::TextDisabled("Linked");
      ImGui::PopID();
      return;
    }
    std::string textureGuid = resource->assetGuid.value;
    AssetFieldOptions options{};
    options.label = label;
    options.requiredType = AssetType::Texture;
    options.drawLabel = false;
    options.width = width;
    if (DrawAssetField(&scene.GetAssetDatabase(), options, textureGuid)) {
      MaterialAssetData before = source;
      resource->assetGuid = AssetGuid{textureGuid};
      resource->enabled = resource->assetGuid.IsValid();
      CommitMaterialMutation(
          std::move(before), "Bind " + resource->displayName, scene);
    }
    trackItem();
    ImGui::PopID();
  };

  if (node.kind == MaterialNodeKind::Parameter) {
    MaterialGraphParameterData *parameter =
        MATERIAL_GRAPH_UI::FindParameter(graph, node.referenceId);
    if (parameter == nullptr) {
      if (ImGui::Button("+ Create Parameter", {width, 0.0f})) {
        MaterialAssetData before = source;
        MaterialGraphParameterData created{};
        created.id = "Parameter_" + std::to_string(authoringIdCounter_++);
        created.displayName = "Parameter";
        created.defaultValue.type = node.valueType;
        created.exposed = true;
        graph.parameters.push_back(std::move(created));
        node.referenceId = graph.parameters.back().id;
        CommitMaterialMutation(std::move(before),
                               "Create material parameter", scene);
      }
      trackItem();
    } else {
      ImGui::TextDisabled("%s",
                          parameter->displayName.empty()
                              ? parameter->id.c_str()
                              : parameter->displayName.c_str());
      MaterialAssetData before = source;
      const bool changed = DrawCompactValue(
          parameter->defaultValue,
          MATERIAL_GRAPH_UI::LooksLikeColor(
              parameter->displayName, parameter->id));
      trackItem();
      if (parameter->defaultValue.type == MaterialValueType::Bool) {
        if (changed) {
          CommitMaterialMutation(std::move(before),
                                 "Edit material parameter", scene);
        }
      } else {
        commitContinuous(std::move(before), "Edit material parameter",
                         changed);
      }
    }
  } else if (node.kind == MaterialNodeKind::Constant) {
    MaterialAssetData before = source;
    const bool changed = DrawCompactValue(
        node.value,
        MATERIAL_GRAPH_UI::LooksLikeColor(node.displayName, node.id));
    trackItem();
    if (node.value.type == MaterialValueType::Bool) {
      if (changed) {
        CommitMaterialMutation(std::move(before), "Edit material constant",
                               scene);
      }
    } else {
      commitContinuous(std::move(before), "Edit material constant", changed);
    }
  } else if (node.kind == MaterialNodeKind::TextureSample) {
    MaterialGraphResourceData *resource =
        MATERIAL_GRAPH_UI::FindGraphResource(graph, node.referenceId);
    if (resource == nullptr) {
      if (ImGui::Button("+ Texture Resource", {width, 0.0f})) {
        MaterialAssetData before = source;
        MaterialGraphResourceData created{};
        created.id = "Texture_" + std::to_string(authoringIdCounter_++);
        created.displayName = "Texture";
        created.type = MaterialResourceType::Texture2D;
        graph.resources.push_back(std::move(created));
        node.referenceId = graph.resources.back().id;
        CommitMaterialMutation(std::move(before),
                               "Create texture sample resource", scene);
      }
      trackItem();
    } else {
      ImGui::TextDisabled("%s",
                          resource->displayName.empty()
                              ? resource->id.c_str()
                              : resource->displayName.c_str());
      std::string textureGuid = resource->assetGuid.value;
      AssetFieldOptions options{};
      options.label = "Node Texture";
      options.requiredType = AssetType::Texture;
      options.drawLabel = false;
      options.width = width;
      if (DrawAssetField(&scene.GetAssetDatabase(), options, textureGuid)) {
        MaterialAssetData before = source;
        resource->assetGuid = AssetGuid{textureGuid};
        resource->enabled = resource->assetGuid.IsValid();
        CommitMaterialMutation(std::move(before),
                               "Bind texture sample asset", scene);
      }
      trackItem();
    }
  } else if (node.kind == MaterialNodeKind::RampDiffuse ||
             node.kind == MaterialNodeKind::MultiRampDiffuse) {
    MaterialGraphResourceData *resource =
        MATERIAL_GRAPH_UI::FindRampResource(graph, node);
    if (resource == nullptr) {
      if (ImGui::Button("+ Ramp Resource", {width, 0.0f})) {
        MaterialAssetData before = source;
        MaterialGraphResourceData created{};
        created.id = "Ramp_" + std::to_string(authoringIdCounter_++);
        created.displayName = "Diffuse Ramp";
        created.type = MaterialResourceType::Ramp1D;
        created.semantic = MaterialResourceSemantic::Ramp;
        graph.resources.push_back(std::move(created));
        node.referenceId = graph.resources.back().id;
        CommitMaterialMutation(std::move(before), "Create Ramp resource",
                               scene);
      }
      trackItem();
    } else {
      const AssetChoice *bound = FindMaterialChoice(resource->assetGuid);
      const char *preview = bound != nullptr
          ? bound->label.c_str()
          : resource->assetGuid.IsValid() ? "Missing Ramp" : "Choose Ramp";
      ImGui::SetNextItemWidth(width);
      if (ImGui::BeginCombo("##RampAsset", preview)) {
        for (const AssetChoice &choice : materialChoices_) {
          if (choice.materialKind != MaterialAssetKind::Ramp) {
            continue;
          }
          if (ImGui::Selectable(choice.label.c_str(),
                                choice.guid == resource->assetGuid)) {
            MaterialAssetData before = source;
            node.referenceId = resource->id;
            resource->assetGuid = choice.guid;
            resource->enabled = true;
            CommitMaterialMutation(std::move(before), "Bind Ramp asset",
                                   scene);
          }
        }
        ImGui::EndCombo();
      }
      trackItem();
      bound = FindMaterialChoice(resource->assetGuid);
      if (bound != nullptr && bound->rampPreview.has_value()) {
        MATERIAL_GRAPH_UI::DrawRampStrip(*bound->rampPreview, width, 20.0f);
        trackItem();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
          (void)BeginInlineRampEdit(scene, bound->guid, statusMessage_);
        }
      }
      if (ImGui::SmallButton("New from Preset...")) {
        ImGui::OpenPopup("##NewRampPreset");
      }
      trackItem();
      if (width >= 200.0f) {
        ImGui::SameLine();
      }
      ImGui::BeginDisabled(bound == nullptr);
      if (ImGui::SmallButton("Edit Ramp")) {
        (void)BeginInlineRampEdit(scene, bound->guid, statusMessage_);
      }
      trackItem();
      ImGui::EndDisabled();
      if (ImGui::BeginPopup("##NewRampPreset")) {
        for (const ASSETS::MATERIAL::MaterialRampPresetInfo &preset :
             ASSETS::MATERIAL::GetBuiltInMaterialRampPresets()) {
          if (ImGui::MenuItem(preset.displayName.data())) {
            (void)CreateRampPresetAndBind(
                scene, source, graph, node, preset.id, statusMessage_);
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", preset.description.data());
          }
        }
        ImGui::EndPopup();
      }
    }
    drawParameter("parameter.shadowThreshold", "Shadow", "ShadowThreshold");
    drawParameter("parameter.shadowSoftness", "Softness", "ShadowSoftness");
    drawParameter("parameter.midThreshold", "Midtone", "MidThreshold");
    drawParameter("parameter.shadowTint", "Shadow Tint", "ShadowTint");
    drawParameter("parameter.midTint", "Mid Tint", "MidTint");
    drawParameter("parameter.litTint", "Lit Tint", "LitTint");
    drawParameter("parameter.rampMaskInfluence", "Mask", "MaskInfluence");
    drawParameter("parameter.rampIntensity", "Ramp Influence", "Intensity");
    drawParameter("parameter.toonDirectGain", "Direct Gain", "DirectGain");
    if (node.kind == MaterialNodeKind::MultiRampDiffuse) {
      drawParameter("parameter.rampRow", "Ramp Row", "RampRow");
      drawParameter("parameter.rampBlend", "Ramp Blend", "RampBlend");
    }
  } else if (node.kind == MaterialNodeKind::PbrSurface) {
    drawParameter("parameter.baseColor", "Base Color", "BaseColor");
    drawTexture("resource.baseColor", "Base Texture", "BaseColor");
    drawParameter("parameter.metallic", "Metallic", "Metallic");
    drawParameter("parameter.roughness", "Roughness", "Roughness");
    drawTexture("resource.metallicRoughness", "Metal/Rough", "Metallic");
    drawTexture("resource.normal", "Normal", "Normal");
    drawParameter("parameter.normalScale", "Normal Scale");
    drawParameter("parameter.occlusionStrength", "Occlusion", "Occlusion");
    drawTexture("resource.occlusion", "Occlusion Map", "Occlusion");
    drawParameter("parameter.specular", "Specular", "Specular");
    drawParameter("parameter.specularColor", "Specular Color", "SpecularColor");
    drawTexture("resource.specular", "Specular Map", "Specular");
    drawTexture("resource.specularColor", "Spec Color Map", "SpecularColor");
    drawParameter("parameter.emissive", "Emission", "Emissive");
    drawParameter("parameter.emissiveStrength", "Emission Power");
    drawTexture("resource.emissive", "Emission Map", "Emissive");
  } else if (node.kind == MaterialNodeKind::SurfaceOutput) {
    ImGui::TextDisabled("Mesh Shader Surface");
  } else if (node.kind == MaterialNodeKind::SpecularBand) {
    drawParameter("parameter.specularBandWidth", "Width", "Width");
    drawParameter("parameter.specularBandSoftness", "Softness", "Softness");
    drawParameter("parameter.specularIntensity", "Intensity", "Intensity");
    drawParameter("parameter.specularPower", "Power", "Power");
    drawParameter("parameter.specularTint", "Tint", "Tint");
  } else if (node.kind == MaterialNodeKind::FresnelRim) {
    drawParameter("parameter.rimColor", "Color", "Color");
    drawParameter("parameter.rimPower", "Power", "Power");
    drawParameter("parameter.rimWidth", "Width", "Width");
    drawParameter("parameter.rimIntensity", "Intensity", "Intensity");
    drawParameter("parameter.rimLightAlignment", "Light Align", "LightAlignment");
  } else if (node.kind == MaterialNodeKind::FaceSdf) {
    capturesPointer = DrawFaceRampNodeControl(
        scene, source, graph, width) || capturesPointer;
    drawTexture("resource.faceSdf", "Face SDF");
    ImGui::TextDisabled("Linear mirrored SDF in R; face basis is model-local.");
    drawParameter("parameter.faceShadowSoftness", "Softness", "Softness");
    drawParameter("parameter.toonDirectGain", "Direct Gain", "DirectGain");
    drawParameter("parameter.faceShadowOffset", "Offset");
    drawParameter("parameter.faceHairShadowStrength", "Hair Shadow");
    drawParameter("parameter.faceHairShadowOffset", "Hair Offset");
  } else if (node.kind == MaterialNodeKind::AnisotropicHair) {
    const char* hairResource =
        MATERIAL_GRAPH_UI::FindGraphResource(graph, "resource.hairControl")
            ? "resource.hairControl" : "resource.strandMask";
    drawTexture(hairResource, "Hair Control", "Mask");
    const MaterialGraphResourceData* hairControl =
        MATERIAL_GRAPH_UI::FindGraphResource(graph, hairResource);
    ImGui::TextDisabled(
        hairControl != nullptr && hairControl->assetGuid.IsValid()
            ? "RG flow | B mask | A per-strand shift"
            : "No control map: uses mesh tangent fallback");
    drawParameter("parameter.hairShift", "Shift", "Shift");
    drawParameter("parameter.hairWidth", "Width", "Width");
    drawParameter("parameter.hairSecondaryShift", "Second Shift");
    drawParameter("parameter.hairIntensity", "Intensity");
    const char* hairTint =
        MATERIAL_GRAPH_UI::FindParameter(graph, "parameter.hairHighlightTint")
            ? "parameter.hairHighlightTint" : "parameter.specularTint";
    drawParameter(hairTint, "Tint");
  } else if (node.kind == MaterialNodeKind::MatCap) {
    drawTexture("resource.matCap", "MatCap");
    drawParameter("parameter.matCapTint", "Tint");
    drawParameter("parameter.matCapIntensity", "Intensity");
  } else if (node.kind == MaterialNodeKind::EyeLighting) {
    const char* eyeResource =
        MATERIAL_GRAPH_UI::FindGraphResource(graph, "resource.eyeControl")
            ? "resource.eyeControl" : "resource.irisMask";
    drawTexture(eyeResource, "Eye Control", "Mask");
    ImGui::TextDisabled("Linear: R iris | G cornea | B catchlight | A depth");
    drawParameter("parameter.irisDepth", "Iris Depth");
    drawParameter("parameter.eyeSparkle", "Catchlight");
    drawParameter("parameter.eyeSparklePower", "Cornea Power");
  } else if (node.kind == MaterialNodeKind::SkinLighting) {
    drawTexture("resource.thicknessMask", "Thickness", "Mask");
    drawParameter("parameter.skinWrap", "Light Wrap", "Wrap");
    drawParameter("parameter.skinSubsurface", "Subsurface", "Subsurface");
    drawParameter("parameter.skinTint", "Scatter Tint", "Tint");
  } else if (node.kind == MaterialNodeKind::FabricSheen) {
    drawTexture("resource.fabricMask", "Fabric Mask", "Mask");
    drawParameter("parameter.fabricSheen", "Sheen", "Sheen");
    drawParameter("parameter.fiberRoughness", "Roughness", "FiberRoughness");
    drawParameter("parameter.fabricSheenPower", "Power", "Power");
    drawParameter("parameter.fabricSheenIntensity", "Intensity", "Intensity");
  } else if (node.kind == MaterialNodeKind::HybridLighting) {
    drawParameter("parameter.directDiffusePbrBlend", "Diffuse PBR", "DirectDiffuseBlend");
    drawParameter("parameter.directSpecularPbrBlend", "Specular PBR", "DirectSpecularBlend");
    drawParameter("parameter.indirectPbrBlend", "Indirect PBR", "IndirectBlend");
  } else if (node.kind == MaterialNodeKind::Outline) {
    MaterialGraphParameterData* mode =
        MATERIAL_GRAPH_UI::FindParameter(graph, "parameter.outlineMode");
    if (mode != nullptr) {
      int current = mode->defaultValue.numeric.x < 0.5f
          ? 0 : mode->defaultValue.numeric.x < 1.5f ? 1 : 2;
      constexpr const char* kModes[]{"Geometry", "Screen", "Both"};
      ImGui::SetNextItemWidth(width);
      if (ImGui::Combo("##OutlineMode", &current, kModes, 3)) {
        MaterialAssetData before = source;
        mode->defaultValue.numeric.x = static_cast<float>(current);
        CommitMaterialMutation(std::move(before), "Change outline mode", scene);
      }
      trackItem();
      if (current != 1) {
        drawParameter("parameter.outlineColor", "Color", "Color");
        drawParameter("parameter.outlineThickness", "Thickness", "Thickness");
      }
      if (current != 0) {
        ImGui::TextDisabled("Screen style: View Outline Profile");
      }
    }
  }

  ImGui::PopItemWidth();
  ImGui::EndGroup();
  ImGui::PopClipRect();
  capturesPointer = capturesPointer || ImGui::IsItemHovered();
  ImGui::PopID();
  ImGui::SetCursorScreenPos(restore);
  return capturesPointer;
#else
  (void)scene;
  (void)source;
  (void)graph;
  (void)node;
  (void)x;
  (void)y;
  (void)width;
  return false;
#endif
}

} // namespace HIKARI::EDITOR
