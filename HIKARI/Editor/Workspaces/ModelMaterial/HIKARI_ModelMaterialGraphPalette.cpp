#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <array>
#include <algorithm>
#include <string_view>

#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
#include "Assets/Material/Graph/HIKARI_MaterialNodeDefinition.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::DrawMaterialGraphNodePalette(
    DocumentSceneBase &scene, MaterialAssetData &source,
    MaterialGraphData &graph, MATH::Vec2 position) {
#if defined(HIKARI_WITH_EDITOR)
  constexpr std::array<std::string_view, 4u> categories{
      "Ready-made Shading", "Surface", "Advanced / Inputs",
      "Advanced / Math & Masks"};
  for (const std::string_view category : categories) {
    if (!ImGui::BeginMenu(category.data())) {
      continue;
    }
    for (const MaterialNodeDefinition& definition :
         GetMaterialNodeDefinitions()) {
      if (!definition.paletteVisible) {
        continue;
      }
      const MaterialNodeKind kind = definition.kind;
      if (MATERIAL_GRAPH_UI::NodeCategory(kind) != category) {
        continue;
      }
      const bool duplicateSingleton =
          ASSETS::MATERIAL::IsSingletonReadyMadeMaterialNode(kind) &&
          std::any_of(graph.nodes.begin(), graph.nodes.end(),
                      [kind](const auto& node) { return node.kind == kind; });
      ImGui::BeginDisabled(duplicateSingleton);
      const std::string_view label =
          ASSETS::MATERIAL::IsReadyMadeMaterialNode(kind)
              ? ASSETS::MATERIAL::ReadyMadeMaterialNodeDisplayName(kind)
              : definition.stableName;
      if (ImGui::MenuItem(label.data())) {
        MaterialAssetData before = source;
        MaterialGraphNodeData node{};
        node.id = "Node_" + std::to_string(authoringIdCounter_++);
        node.kind = kind;
        node.displayName = std::string(definition.stableName);
        node.position = position;
        node.valueType = MATERIAL_GRAPH_UI::DefaultNodeValueType(kind);
        node.value.type = node.valueType;
        ASSETS::MATERIAL::InitializeReadyMadeMaterialNode(source, node);
        if (kind == MaterialNodeKind::RampDiffuse ||
            kind == MaterialNodeKind::MultiRampDiffuse ||
            kind == MaterialNodeKind::FaceSdf) {
          (void)BindBuiltInRampPresets(source);
        }
        if (kind == MaterialNodeKind::Parameter && !graph.parameters.empty()) {
          node.referenceId = graph.parameters.front().id;
        }
        if (kind == MaterialNodeKind::SurfaceOutput) {
          graph.outputNodeId = node.id;
        }
        selectedGraphNodeId_ = node.id;
        graph.nodes.push_back(std::move(node));
        CommitMaterialMutation(std::move(before), "Add material graph node",
                               scene);
      }
      ImGui::EndDisabled();
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Material Functions")) {
    bool foundFunction = false;
    for (const AssetChoice& choice : materialChoices_) {
      if (!choice.ready ||
          choice.materialKind != MaterialAssetKind::Function ||
          choice.functionOutputs.empty()) {
        continue;
      }
      foundFunction = true;
      if (!ImGui::MenuItem(choice.label.c_str())) {
        continue;
      }
      MaterialAssetData before = source;
      MaterialGraphNodeData node{};
      node.id = "Node_" + std::to_string(authoringIdCounter_++);
      node.kind = MaterialNodeKind::FunctionCall;
      node.displayName = choice.label;
      node.position = position;
      node.assetReference = choice.guid;
      node.functionInputs = choice.functionInputs;
      node.functionOutputs = choice.functionOutputs;
      node.valueType = choice.functionOutputs.front().defaultValue.type;
      node.value.type = node.valueType;
      selectedGraphNodeId_ = node.id;
      graph.nodes.push_back(std::move(node));
      CommitMaterialMutation(
          std::move(before), "Add Material Function node", scene);
    }
    if (!foundFunction) {
      ImGui::BeginDisabled();
      (void)ImGui::MenuItem("No imported Material Functions");
      ImGui::EndDisabled();
    }
    ImGui::EndMenu();
  }
#else
  (void)scene;
  (void)source;
  (void)graph;
  (void)position;
#endif
}

} // namespace HIKARI::EDITOR
