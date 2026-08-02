#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <string>

#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

bool IsRampNode(MaterialNodeKind kind) noexcept {
  return kind == MaterialNodeKind::RampDiffuse ||
         kind == MaterialNodeKind::MultiRampDiffuse;
}

bool IsCompatibleResource(const MaterialGraphNodeData &node,
                          const MaterialGraphResourceData &resource) noexcept {
  if (IsRampNode(node.kind)) {
    return resource.semantic == MaterialResourceSemantic::Ramp ||
           resource.type == MaterialResourceType::Ramp1D ||
           resource.type == MaterialResourceType::Ramp2D;
  }
  if (node.kind == MaterialNodeKind::MatCap) {
    return resource.semantic == MaterialResourceSemantic::MatCap;
  }
  if (node.kind == MaterialNodeKind::FaceSdf) {
    return resource.semantic == MaterialResourceSemantic::FaceSdf;
  }
  return node.kind == MaterialNodeKind::HairShadow &&
         resource.semantic == MaterialResourceSemantic::HairShadow;
}

MaterialGraphResourceData *FindResource(MaterialGraphData &graph,
                                        std::string_view id) noexcept {
  const auto found = std::find_if(
      graph.resources.begin(), graph.resources.end(),
      [id](const MaterialGraphResourceData &resource) {
        return resource.id == id;
      });
  return found == graph.resources.end() ? nullptr : &*found;
}

} // namespace

void ModelMaterialWorkspaceController::
    DrawMaterialGraphResponseResourceInspector(
        DocumentSceneBase &scene, MaterialAssetData &source,
        MaterialGraphData &graph, MaterialGraphNodeData &node) {
#if defined(HIKARI_WITH_EDITOR)
  if (IsRampNode(node.kind)) {
    ImGui::SeparatorText("Ramp Resources");
    size_t rampCount = 0u;
    for (MaterialGraphResourceData &resource : graph.resources) {
      if (!IsCompatibleResource(node, resource)) {
        continue;
      }
      ++rampCount;
      ImGui::PushID(resource.id.c_str());
      ImGui::TextUnformatted(resource.displayName.empty()
                                 ? resource.id.c_str()
                                 : resource.displayName.c_str());
      const AssetChoice *bound = FindMaterialChoice(resource.assetGuid);
      const char *label = bound != nullptr
                              ? bound->label.c_str()
                          : resource.assetGuid.IsValid()
                              ? "Missing Ramp Asset"
                              : "Choose Ramp Asset";
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::BeginCombo("##RampAsset", label)) {
        if (ImGui::Selectable("None", !resource.assetGuid.IsValid())) {
          MaterialAssetData before = source;
          resource.assetGuid = {};
          resource.enabled = false;
          CommitMaterialMutation(std::move(before),
                                 "Clear Ramp resource", scene);
        }
        for (const AssetChoice &choice : materialChoices_) {
          if (choice.materialKind != MaterialAssetKind::Ramp) {
            continue;
          }
          if (ImGui::Selectable(choice.label.c_str(),
                                choice.guid == resource.assetGuid)) {
            MaterialAssetData before = source;
            resource.assetGuid = choice.guid;
            resource.enabled = true;
            CommitMaterialMutation(std::move(before),
                                   "Bind Ramp resource", scene);
          }
        }
        ImGui::EndCombo();
      }
      ImGui::TextDisabled("%s | generated linear ramp texture",
                          ToString(resource.type).data());
      ImGui::PopID();
    }
    const size_t desiredCount =
        node.kind == MaterialNodeKind::MultiRampDiffuse ? 2u : 1u;
    if (rampCount < desiredCount &&
        ImGui::Button("Add Ramp Resource", ImVec2(-1.0f, 0.0f))) {
      MaterialAssetData before = source;
      MaterialGraphResourceData resource{};
      resource.id = "Ramp_" + std::to_string(authoringIdCounter_++);
      resource.displayName = rampCount == 0u
                                 ? "Diffuse Ramp"
                                 : "Secondary Ramp";
      resource.type = node.kind == MaterialNodeKind::MultiRampDiffuse
                          ? MaterialResourceType::Ramp2D
                          : MaterialResourceType::Ramp1D;
      resource.semantic = MaterialResourceSemantic::Ramp;
      graph.resources.push_back(std::move(resource));
      if (node.referenceId.empty()) {
        node.referenceId = graph.resources.back().id;
      }
      CommitMaterialMutation(std::move(before),
                             "Add Ramp resource", scene);
    }
    return;
  }

  const MaterialResourceSemantic requiredSemantic =
      node.kind == MaterialNodeKind::MatCap
          ? MaterialResourceSemantic::MatCap
      : node.kind == MaterialNodeKind::FaceSdf
          ? MaterialResourceSemantic::FaceSdf
          : MaterialResourceSemantic::HairShadow;
  const char *requiredLabel = node.kind == MaterialNodeKind::MatCap
                                  ? "MatCap Texture"
                              : node.kind == MaterialNodeKind::FaceSdf
                                  ? "Face SDF Texture"
                                  : "Hair Shadow Texture (linear R)";
  MaterialGraphResourceData *resource = FindResource(graph, node.referenceId);
  if (resource == nullptr || !IsCompatibleResource(node, *resource)) {
    resource = nullptr;
    for (MaterialGraphResourceData &candidate : graph.resources) {
      if (IsCompatibleResource(node, candidate)) {
        resource = &candidate;
        break;
      }
    }
  }
  if (resource == nullptr) {
    if (ImGui::Button("Create Required Texture Resource",
                      ImVec2(-1.0f, 0.0f))) {
      MaterialAssetData before = source;
      MaterialGraphResourceData created{};
      created.id = node.kind == MaterialNodeKind::MatCap
                       ? "resource.matCap"
                   : node.kind == MaterialNodeKind::FaceSdf
                       ? "resource.faceSdf"
                       : "resource.hairShadow";
      created.displayName = requiredLabel;
      created.type = MaterialResourceType::Texture2D;
      created.semantic = requiredSemantic;
      graph.resources.push_back(std::move(created));
      node.referenceId = graph.resources.back().id;
      CommitMaterialMutation(std::move(before),
                             "Create response texture resource", scene);
    }
    return;
  }

  std::string textureGuid = resource->assetGuid.value;
  AssetFieldOptions fieldOptions{};
  fieldOptions.label = requiredLabel;
  fieldOptions.requiredType = AssetType::Texture;
  if (DrawAssetField(&scene.GetAssetDatabase(), fieldOptions, textureGuid)) {
    MaterialAssetData before = source;
    node.referenceId = resource->id;
    resource->assetGuid = AssetGuid{textureGuid};
    resource->enabled = resource->assetGuid.IsValid();
    CommitMaterialMutation(std::move(before),
                           "Bind response texture", scene);
  }
  ImGui::TextDisabled("Semantic: %s | Color Space: %s",
                      ToString(requiredSemantic).data(),
                      requiredSemantic == MaterialResourceSemantic::MatCap
                          ? "sRGB"
                          : "Linear");
  int uvSet = static_cast<int>(resource->texCoord);
  if (ImGui::Combo("UV Set", &uvSet, "UV0\0UV1\0")) {
    MaterialAssetData before = source;
    resource->texCoord = static_cast<uint32_t>(uvSet);
    CommitMaterialMutation(std::move(before),
                           "Change response texture UV set", scene);
  }
#else
  (void)scene;
  (void)source;
  (void)graph;
  (void)node;
#endif
}

} // namespace HIKARI::EDITOR
