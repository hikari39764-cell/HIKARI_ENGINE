#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <array>
#include <algorithm>
#include <string_view>

#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
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
    for (uint32_t value = static_cast<uint32_t>(MaterialNodeKind::Constant);
         value <= static_cast<uint32_t>(MaterialNodeKind::HairShadow);
         ++value) {
      const auto kind = static_cast<MaterialNodeKind>(value);
      if (kind == MaterialNodeKind::FunctionCall) {
        continue;
      }
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
              : ToString(kind);
      if (ImGui::MenuItem(label.data())) {
        MaterialAssetData before = source;
        MaterialGraphNodeData node{};
        node.id = "Node_" + std::to_string(authoringIdCounter_++);
        node.kind = kind;
        node.displayName = std::string(ToString(kind));
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
        } else if (kind == MaterialNodeKind::TextureSample &&
                   !graph.resources.empty()) {
          node.referenceId = graph.resources.front().id;
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
#else
  (void)scene;
  (void)source;
  (void)graph;
  (void)position;
#endif
}

} // namespace HIKARI::EDITOR
