#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>

#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::DrawMaterialGraphInspector(
    DocumentSceneBase &scene, MaterialAssetData &source,
    MaterialGraphData &graph) {
#if defined(HIKARI_WITH_EDITOR)
  (void)scene;
  (void)source;
  if (const MaterialGraphNodeData *node =
          MATERIAL_GRAPH_UI::FindNode(graph, selectedGraphNodeId_)) {
    ImGui::TextDisabled("CURRENT NODE");
    ImGui::TextWrapped("%s",
                       node->displayName.empty()
                           ? ToString(node->kind).data()
                           : node->displayName.c_str());
    ImGui::TextDisabled("%s / %s",
                        MATERIAL_GRAPH_UI::NodeCategory(node->kind).data(),
                        ToString(node->kind).data());
    ImGui::TextDisabled("%s", node->id.c_str());
    ImGui::TextWrapped(
        "Edit values, images, Ramp assets and node modes directly on the "
        "card. This panel explains the contract and reports diagnostics; it "
        "does not keep a second copy of node controls.");

    if (ImGui::CollapsingHeader(
            "Node Contract", ImGuiTreeNodeFlags_DefaultOpen)) {
      const auto &contract =
          ASSETS::MATERIAL::GetMaterialNodeSemanticContract(node->kind);
      ImGui::TextDisabled("CONNECT TO");
      ImGui::TextWrapped("%.*s", static_cast<int>(contract.connectsTo.size()),
                         contract.connectsTo.data());
      ImGui::TextDisabled("AFFECTED BY");
      ImGui::TextWrapped("%.*s", static_cast<int>(contract.affectedBy.size()),
                         contract.affectedBy.data());
      ImGui::TextDisabled("PROVIDES");
      ImGui::TextWrapped("%.*s", static_cast<int>(contract.provides.size()),
                         contract.provides.data());
      ImGui::TextDisabled("DEFAULT / FAILURE");
      ImGui::TextWrapped("%.*s",
                         static_cast<int>(contract.defaultBehavior.size()),
                         contract.defaultBehavior.data());
      ImGui::TextWrapped("%.*s",
                         static_cast<int>(contract.failureBehavior.size()),
                         contract.failureBehavior.data());
    }
  } else {
    ImGui::TextDisabled("CURRENT NODE");
    ImGui::TextWrapped(
        "Select a node. Its authoring controls stay on the graph card.");
  }

  if (!selectedGraphEdgeId_.empty()) {
    const auto selectedEdge = std::find_if(
        graph.edges.begin(), graph.edges.end(), [this](const auto &edge) {
          return edge.id == selectedGraphEdgeId_;
        });
    if (selectedEdge != graph.edges.end()) {
      ImGui::SeparatorText("Selected Connection");
      ImGui::TextWrapped("%s.%s -> %s.%s",
                         selectedEdge->sourceNodeId.c_str(),
                         selectedEdge->sourcePin.c_str(),
                         selectedEdge->targetNodeId.c_str(),
                         selectedEdge->targetPin.c_str());
    }
  }
  ImGui::BeginDisabled(selectedGraphEdgeId_.empty());
  const bool requestDeleteEdge = ImGui::Button("Delete Connection");
  ImGui::EndDisabled();

  const MaterialCompileResult &compile = authoringDocument_.GetCompileResult();
  const uint64_t materialRevision = authoringDocument_.GetMaterialRevision();
  const uint64_t compileRevision = authoringDocument_.GetCompileRevision();
  const bool currentProgram = compile.success &&
      materialRevision == compileRevision;
  const ImVec4 statusColor = currentProgram
      ? ImVec4{0.28f, 0.82f, 0.48f, 1.0f}
      : ImVec4{1.0f, 0.30f, 0.30f, 1.0f};
  ImGui::SeparatorText("Runtime Preview Status");
  ImGui::TextColored(statusColor, "%s",
                     currentProgram ? "CURRENT V5 GRAPH IS LIVE"
                                    : "CURRENT V5 GRAPH IS INVALID - PREVIEW PAUSED");
  ImGui::Text("Source revision %llu | compiled revision %llu",
              static_cast<unsigned long long>(materialRevision),
              static_cast<unsigned long long>(compileRevision));
  if (ImGui::CollapsingHeader(
          "Compile & Diagnostics",
          currentProgram ? ImGuiTreeNodeFlags_None
                         : ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::TextUnformatted(compile.success ? "Compiled" : "Compile failed");
    for (const MaterialCompileDiagnostic &diagnostic : compile.diagnostics) {
      ImGui::BulletText("%s: %s", diagnostic.code.c_str(),
                        diagnostic.message.c_str());
    }
  }
  return requestDeleteEdge;
#else
  (void)scene;
  (void)source;
  (void)graph;
  return false;
#endif
}

} // namespace HIKARI::EDITOR
