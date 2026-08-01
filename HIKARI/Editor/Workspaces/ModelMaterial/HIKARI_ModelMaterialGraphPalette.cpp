#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <array>
#include <string_view>

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
  constexpr std::array<std::string_view, 5u> categories{
      "Inputs", "Math & Masks", "Surface", "Lighting Responses", "Functions"};
  for (const std::string_view category : categories) {
    ImGui::SeparatorText(category.data());
    for (uint32_t value = static_cast<uint32_t>(MaterialNodeKind::Constant);
         value <= static_cast<uint32_t>(MaterialNodeKind::FunctionCall);
         ++value) {
      const auto kind = static_cast<MaterialNodeKind>(value);
      if (MATERIAL_GRAPH_UI::NodeCategory(kind) != category) {
        continue;
      }
      const bool duplicateOutput =
          kind == MaterialNodeKind::SurfaceOutput && !graph.outputNodeId.empty();
      ImGui::BeginDisabled(duplicateOutput);
      if (ImGui::MenuItem(ToString(kind).data())) {
        MaterialAssetData before = source;
        MaterialGraphNodeData node{};
        node.id = "Node_" + std::to_string(authoringIdCounter_++);
        node.kind = kind;
        node.displayName = std::string(ToString(kind));
        node.position = position;
        node.valueType = MATERIAL_GRAPH_UI::DefaultNodeValueType(kind);
        node.value.type = node.valueType;
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
  }
#else
  (void)scene;
  (void)source;
  (void)graph;
  (void)position;
#endif
}

} // namespace HIKARI::EDITOR
