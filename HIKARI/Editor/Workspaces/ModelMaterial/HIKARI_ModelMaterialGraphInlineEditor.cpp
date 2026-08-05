#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphDefaultInputs.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
bool ModelMaterialWorkspaceController::DrawMaterialGraphNodeInlineControls(
    DocumentSceneBase &scene, MaterialAssetData &source,
    MaterialGraphData &graph, MaterialGraphNodeData &node,
    float x, float y, float width) {
#if defined(HIKARI_WITH_EDITOR)
  const ImVec2 restore = ImGui::GetCursorScreenPos();
  bool capturesPointer = false;
  ImGui::PushID(node.id.c_str());
  ImGui::SetCursorScreenPos({x, y});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(5.0f, 4.0f));
  ImGui::PushClipRect(
      {x, y},
      {x + width,
       y + MATERIAL_GRAPH_UI::NodeInlineHeight(node.kind)},
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
    const bool changed = MATERIAL_GRAPH_UI::DrawCompactMaterialValue(
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
      const bool changed = MATERIAL_GRAPH_UI::DrawCompactMaterialValue(
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
    const bool changed = MATERIAL_GRAPH_UI::DrawCompactMaterialValue(
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
      constexpr MaterialResourceSemantic kTextureSemantics[]{
          MaterialResourceSemantic::Custom,
          MaterialResourceSemantic::BaseColor,
          MaterialResourceSemantic::Normal,
          MaterialResourceSemantic::MetallicRoughness,
          MaterialResourceSemantic::Occlusion,
          MaterialResourceSemantic::Emissive,
          MaterialResourceSemantic::Specular,
          MaterialResourceSemantic::SpecularColor,
          MaterialResourceSemantic::Mask,
          MaterialResourceSemantic::MatCap,
          MaterialResourceSemantic::FaceSdf,
          MaterialResourceSemantic::HairShadow,
          MaterialResourceSemantic::HairControl,
          MaterialResourceSemantic::EyeControl,
          MaterialResourceSemantic::Thickness,
          MaterialResourceSemantic::Fabric,
      };
      int semanticIndex = 0;
      for (int index = 0;
           index < static_cast<int>(std::size(kTextureSemantics)); ++index) {
        if (kTextureSemantics[index] == resource->semantic) {
          semanticIndex = index;
          break;
        }
      }
      const auto semanticLabel = [](void *data, int index,
                                    const char **text) {
        const auto *semantics =
            static_cast<const MaterialResourceSemantic *>(data);
        *text = ToString(semantics[index]).data();
        return true;
      };
      ImGui::SetNextItemWidth(width);
      if (ImGui::Combo("##TextureSemantic", &semanticIndex,
                       semanticLabel,
                       const_cast<MaterialResourceSemantic *>(
                           kTextureSemantics),
                       static_cast<int>(std::size(kTextureSemantics)))) {
        MaterialAssetData before = source;
        resource->semantic = kTextureSemantics[semanticIndex];
        resource->displayName =
            std::string(ToString(resource->semantic)) + " Texture";
        CommitMaterialMutation(std::move(before),
                               "Change texture sample semantic", scene);
      }
      trackItem();
      ImGui::TextDisabled(
          "Color space: %s (derived from semantic)",
          resource->semantic == MaterialResourceSemantic::BaseColor ||
                  resource->semantic == MaterialResourceSemantic::Emissive ||
                  resource->semantic == MaterialResourceSemantic::SpecularColor ||
                  resource->semantic == MaterialResourceSemantic::MatCap
              ? "sRGB"
              : "Linear");
      int uvSet = static_cast<int>(resource->texCoord);
      ImGui::SetNextItemWidth(width);
      if (ImGui::Combo("##TextureUvSet", &uvSet, "UV0\0UV1\0")) {
        MaterialAssetData before = source;
        resource->texCoord = static_cast<uint32_t>(uvSet);
        CommitMaterialMutation(std::move(before),
                               "Change texture sample UV set", scene);
      }
      trackItem();
    }
  } else if (node.kind == MaterialNodeKind::RampDiffuse ||
             node.kind == MaterialNodeKind::MultiRampDiffuse) {
    capturesPointer = DrawMaterialGraphRampNodeInlineControls(
        scene, source, graph, node, width) || capturesPointer;
  } else if (node.kind == MaterialNodeKind::PbrSurface) {
    ImGui::SeparatorText("Surface");
    drawParameter("parameter.baseColor", "Base Color", "BaseColor");
    drawParameter("parameter.metallic", "Metallic", "Metallic");
    drawParameter("parameter.roughness", "Roughness", "Roughness");
    drawParameter("parameter.specular", "Specular", "Specular");
    drawParameter("parameter.specularColor", "Specular Color", "SpecularColor");
    drawParameter("parameter.normalScale", "Normal Scale");
    drawParameter("parameter.occlusionStrength", "Occlusion", "Occlusion");
    ImGui::SeparatorText("Emission");
    drawParameter("parameter.emissive", "Emission", "Emissive");
    drawParameter("parameter.emissiveStrength", "Emission Power");
    ImGui::TextDisabled("Use Texture Sample nodes for every image input.");
  } else if (node.kind == MaterialNodeKind::SurfaceOutput) {
    ImGui::SeparatorText("Mesh Shader Surface");
    int alphaMode = static_cast<int>(source.surface.alphaMode);
    constexpr const char* kAlphaModes[]{"Opaque", "Mask", "Blend"};
    ImGui::SetNextItemWidth(width);
    if (ImGui::Combo("##SurfaceAlphaMode", &alphaMode, kAlphaModes, 3)) {
      MaterialAssetData before = source;
      source.surface.alphaMode = static_cast<MaterialAlphaMode>(alphaMode);
      CommitMaterialMutation(std::move(before), "Change surface alpha mode", scene);
    }
    trackItem();
    if (source.surface.alphaMode == MaterialAlphaMode::Mask) {
      ImGui::TextUnformatted("Alpha Cutoff");
      ImGui::SetNextItemWidth(width);
      MaterialAssetData before = source;
      const bool changed = ImGui::SliderFloat(
          "##SurfaceAlphaCutoff", &source.surface.alphaCutoff,
          0.0f, 1.0f, "%.3f");
      trackItem();
      commitContinuous(
          std::move(before), "Edit surface alpha cutoff", changed);
    } else if (source.surface.alphaMode == MaterialAlphaMode::Blend) {
      ImGui::TextUnformatted("Source Color");
      int representation = static_cast<int>(source.surface.alphaRepresentation);
      constexpr const char *kRepresentations[]{"Straight", "Premultiplied"};
      ImGui::SetNextItemWidth(width);
      if (ImGui::Combo("##SurfaceAlphaRepresentation", &representation, kRepresentations, 2)) {
        MaterialAssetData before = source;
        source.surface.alphaRepresentation = static_cast<MaterialAlphaRepresentation>(representation);
        CommitMaterialMutation(std::move(before), "Change alpha representation", scene);
      }
      trackItem();
      ImGui::TextWrapped(source.surface.alphaRepresentation ==
              MaterialAlphaRepresentation::Premultiplied
              ? "Advanced: source pixels already contain RGB *= Alpha. "
                "Ordinary PNG/FBX eye overlays are usually not premultiplied."
              : "Recommended for ordinary PNG/FBX textures and eye overlays. "
                "The renderer premultiplies the lit result at output.");
    }
    MaterialAssetData before = source;
    if (ImGui::Checkbox("Double Sided", &source.surface.doubleSided)) {
      CommitMaterialMutation(
          std::move(before), "Toggle double-sided surface", scene);
    }
    trackItem();
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
    ImGui::TextDisabled("Required: Texture Sample R -> SDF (linear mirrored SDF).");
    MaterialGraphParameterData *faceRight =
        MATERIAL_GRAPH_UI::FindParameter(graph, "parameter.faceRight");
    MaterialGraphParameterData *faceForward =
        MATERIAL_GRAPH_UI::FindParameter(graph, "parameter.faceForward");
    if (faceRight != nullptr && faceForward != nullptr) {
      const MATH::Vec4 current = faceForward->defaultValue.numeric;
      int forwardAxis = std::abs(current.z) >= std::abs(current.x)
          ? (current.z >= 0.0f ? 0 : 1)
          : (current.x >= 0.0f ? 2 : 3);
      constexpr const char *kForwardAxes[]{"+Z", "-Z", "+X", "-X"};
      ImGui::SetNextItemWidth(width);
      if (ImGui::Combo("##FaceForwardAxis", &forwardAxis,
                       kForwardAxes, 4)) {
        MaterialAssetData before = source;
        constexpr MATH::Vec4 kForward[]{
            {0.0f, 0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, -1.0f, 0.0f},
            {1.0f, 0.0f, 0.0f, 0.0f},
            {-1.0f, 0.0f, 0.0f, 0.0f},
        };
        constexpr MATH::Vec4 kRight[]{
            {1.0f, 0.0f, 0.0f, 0.0f},
            {-1.0f, 0.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, -1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f},
        };
        faceForward->defaultValue.numeric = kForward[forwardAxis];
        faceRight->defaultValue.numeric = kRight[forwardAxis];
        CommitMaterialMutation(std::move(before),
                               "Change Face SDF head forward axis", scene);
      }
      trackItem();
    }
    drawParameter("parameter.faceShadowSoftness", "Softness", "Softness");
    drawParameter("parameter.toonDirectGain", "Direct Gain", "DirectGain");
    drawParameter("parameter.faceShadowOffset", "Offset");
  } else if (node.kind == MaterialNodeKind::AnisotropicHair) {
    ImGui::SeparatorText("Strand Direction");
    ImGui::TextDisabled("Optional HairControl Texture Sample B -> Mask.");
    ImGui::TextDisabled("No map uses mesh tangent; RG flow and A shift specialize it.");
    ImGui::SeparatorText("Dual Kajiya-Kay Lobes");
    drawParameter("parameter.hairShift", "Shift", "Shift");
    drawParameter("parameter.hairWidth", "Width", "Width");
    drawParameter("parameter.hairSecondaryShift", "Second Shift");
    drawParameter("parameter.hairIntensity", "Intensity");
    drawParameter("parameter.hairPrimaryPower", "Primary Sharpness",
                  "PrimaryPower");
    drawParameter("parameter.hairSecondaryPower", "Second Sharpness",
                  "SecondaryPower");
    drawParameter("parameter.hairSecondaryIntensity", "Second Intensity",
                  "SecondaryIntensity");
    drawParameter("parameter.hairFlowStrength", "Flow Strength",
                  "FlowStrength");
    const char* hairTint =
        MATERIAL_GRAPH_UI::FindParameter(graph, "parameter.hairHighlightTint")
            ? "parameter.hairHighlightTint" : "parameter.specularTint";
    drawParameter(hairTint, "Tint");
  } else if (node.kind == MaterialNodeKind::MatCap) {
    ImGui::TextDisabled("Required: MatCap Texture Sample Color -> Image.");
    drawParameter("parameter.matCapTint", "Tint");
    drawParameter("parameter.matCapIntensity", "Intensity");
    drawParameter("parameter.matCapViewCorrection", "View Correction");
    drawParameter("parameter.matCapSphericalNormalBlend", "Eye Sphere Normal");
  } else if (node.kind == MaterialNodeKind::EyeLighting) {
    capturesPointer = DrawEyeLightingNodeInlineControls(scene, source, graph, width) || capturesPointer;
  } else if (node.kind == MaterialNodeKind::CorneaHighlight) {
    capturesPointer = DrawCorneaHighlightNodeInlineControls(
        scene, source, graph, width) || capturesPointer;
  } else if (node.kind == MaterialNodeKind::SkinLighting) {
    ImGui::TextDisabled("Optional Thickness Texture Sample R -> Mask.");
    drawParameter("parameter.skinWrap", "Light Wrap", "Wrap");
    drawParameter("parameter.skinSubsurface", "Subsurface", "Subsurface");
    drawParameter("parameter.skinTint", "Scatter Tint", "Tint");
  } else if (node.kind == MaterialNodeKind::FabricSheen) {
    ImGui::TextDisabled("Optional Fabric Texture Sample B -> Mask.");
    drawParameter("parameter.fabricSheen", "Sheen", "Sheen");
    drawParameter("parameter.fiberRoughness", "Roughness", "FiberRoughness");
    drawParameter("parameter.fabricSheenPower", "Power", "Power");
    drawParameter("parameter.fabricSheenIntensity", "Intensity", "Intensity");
  } else if (node.kind == MaterialNodeKind::HybridLighting) {
    ImGui::TextDisabled("0 = NPR endpoint | 1 = PBR endpoint");
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
        DrawOutlineThicknessPresets(scene, source, graph);
        ImGui::TextDisabled(MESHRENDERER::HasGeometryOutlineFeatureBackend() ? "Geometry backend: ready (closed meshes work best)" : "Geometry backend: unavailable; check renderer diagnostics");
      }
      if (current != 0) {
        ImGui::TextDisabled(
            "Screen outline uses the View profile and supports hair cards.");
      }
    }
  } else if (node.kind == MaterialNodeKind::HairShadow) {
    ImGui::TextDisabled("Required: HairShadow Texture Sample R -> ShadowMask.");
    ImGui::TextDisabled("Linear R: 1 lit visibility | 0 hair shadow.");
    drawParameter(
        "parameter.hairShadowStrength", "Strength", "Strength");
  }
  ImGui::PopItemWidth();
  ImGui::EndGroup();
  ImGui::PopClipRect();
  ImGui::PopStyleVar(2);
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
