#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <utility>

#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::DrawMaterialGraphToolbar(
    DocumentSceneBase &scene, MaterialAssetData &source,
    MaterialGraphData &graph, bool &requestFrameAll,
    bool &requestFocusSelection, bool &requestDeleteNode,
    bool &requestDeleteEdge) {
#if defined(HIKARI_WITH_EDITOR)
  if (ImGui::Button("Add Node...")) {
    ImGui::OpenPopup("##MaterialGraphAddNode");
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!CanUndo());
  if (ImGui::Button("Undo")) {
    (void)Undo(scene, statusMessage_);
    ImGui::EndDisabled();
    return true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!CanRedo());
  if (ImGui::Button("Redo")) {
    (void)Redo(scene, statusMessage_);
    ImGui::EndDisabled();
    return true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  requestFrameAll = ImGui::Button("Frame All");
  ImGui::SameLine();
  ImGui::BeginDisabled(selectedGraphNodeId_.empty());
  requestFocusSelection = ImGui::Button("Focus Selection");
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Organize...")) {
    ImGui::OpenPopup("##MaterialGraphOrganize");
  }
  if (ImGui::BeginPopup("##MaterialGraphOrganize")) {
    const bool hasSelection = !selectedGraphNodeId_.empty();
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::MenuItem("Collapse Selected")) {
      collapsedGraphNodeIds_.insert(selectedGraphNodeId_);
    }
    if (ImGui::MenuItem("Expand Selected")) {
      collapsedGraphNodeIds_.erase(selectedGraphNodeId_);
    }
    ImGui::EndDisabled();
    if (ImGui::MenuItem("Collapse All")) {
      for (const MaterialGraphNodeData &node : graph.nodes) {
        collapsedGraphNodeIds_.insert(node.id);
      }
    }
    if (ImGui::MenuItem("Expand All")) {
      collapsedGraphNodeIds_.clear();
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  const MaterialGraphNodeData *selectedNode =
      MATERIAL_GRAPH_UI::FindNode(graph, selectedGraphNodeId_);
  const bool canDeleteNode = selectedNode != nullptr &&
      selectedNode->kind != MaterialNodeKind::SurfaceOutput;
  ImGui::BeginDisabled(!canDeleteNode && selectedGraphEdgeId_.empty());
  if (ImGui::Button("Delete Selection")) {
    requestDeleteNode = canDeleteNode;
    requestDeleteEdge = !requestDeleteNode &&
        !selectedGraphEdgeId_.empty();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled(
      "Drag sockets to connect/rewire | click a wire to select | Del remove");
  if (ImGui::BeginPopup("##MaterialGraphAddNode")) {
    const MATH::Vec2 position{
        (360.0f - graphCanvas_.pan.x) / graphCanvas_.zoom,
        (180.0f - graphCanvas_.pan.y) / graphCanvas_.zoom};
    DrawMaterialGraphNodePalette(scene, source, graph, position);
    ImGui::EndPopup();
  }
  return false;
#else
  (void)scene;
  (void)source;
  (void)graph;
  (void)requestFrameAll;
  (void)requestFocusSelection;
  (void)requestDeleteNode;
  (void)requestDeleteEdge;
  return false;
#endif
}

void ModelMaterialWorkspaceController::FinishMaterialGraphCanvasInteraction(
    DocumentSceneBase &scene, MaterialAssetData &source,
    MaterialGraphData &graph, std::string completedTargetNode,
    std::string completedTargetPin, bool linkReleased,
    bool mouseInsideCanvas, bool pointerOverNode,
    const std::string &hoveredNodeId,
    const std::string &hoveredEdgeId, MATH::Vec2 canvasOrigin,
    MATH::Vec2 pointer, bool requestDeleteNode,
    bool requestDeleteEdge) {
#if defined(HIKARI_WITH_EDITOR)
  const bool hasCompletedLink = !completedTargetNode.empty() &&
      !completedTargetPin.empty();
  if (hasCompletedLink) {
    std::string reason{};
    if (CanConnectMaterialGraphPins(
            graph, edgeSourceNodeId_, edgeSourcePin_, completedTargetNode,
            completedTargetPin, reason)) {
      MaterialAssetData before = source;
      const bool reconnecting = !rewiredGraphEdgeId_.empty();
      std::erase_if(graph.edges, [&](const MaterialGraphEdgeData &edge) {
        return (reconnecting && edge.id == rewiredGraphEdgeId_) ||
               (edge.targetNodeId == completedTargetNode &&
                edge.targetPin == completedTargetPin);
      });
      MaterialGraphEdgeData edge{};
      edge.id = reconnecting
                    ? rewiredGraphEdgeId_
                    : "Edge_" + std::to_string(authoringIdCounter_++);
      edge.sourceNodeId = edgeSourceNodeId_;
      edge.sourcePin = edgeSourcePin_;
      edge.targetNodeId = std::move(completedTargetNode);
      edge.targetPin = std::move(completedTargetPin);
      selectedGraphNodeId_.clear();
      selectedGraphEdgeId_ = edge.id;
      graph.edges.push_back(std::move(edge));
      CommitMaterialMutation(
          std::move(before), reconnecting
              ? "Reconnect material graph input"
              : "Connect material graph nodes", scene);
    } else {
      statusMessage_ = "Cannot connect sockets: " + reason;
    }
  } else if (linkReleased && !rewiredGraphEdgeId_.empty() &&
             mouseInsideCanvas) {
    MaterialAssetData before = source;
    const std::string disconnectedId = rewiredGraphEdgeId_;
    std::erase_if(graph.edges, [&](const MaterialGraphEdgeData &edge) {
      return edge.id == disconnectedId;
    });
    selectedGraphEdgeId_.clear();
    CommitMaterialMutation(std::move(before),
                           "Disconnect material graph input", scene);
  }
  if (linkReleased) {
    graphLinkDragActive_ = false;
    rewiredGraphEdgeId_.clear();
    edgeSourceNodeId_.clear();
    edgeSourcePin_.clear();
    edgeTargetNodeId_.clear();
    edgeTargetPin_.clear();
  }

  static MATH::Vec2 contextNodePosition{};
  if (mouseInsideCanvas &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
    if (pointerOverNode) {
      selectedGraphNodeId_ = hoveredNodeId;
      selectedGraphEdgeId_.clear();
      ImGui::OpenPopup("##MaterialGraphSelectionContext");
    } else if (!hoveredEdgeId.empty()) {
      selectedGraphNodeId_.clear();
      selectedGraphEdgeId_ = hoveredEdgeId;
      ImGui::OpenPopup("##MaterialGraphSelectionContext");
    } else {
      selectedGraphNodeId_.clear();
      selectedGraphEdgeId_.clear();
      contextNodePosition = NodeGraphCanvas::ScreenToCanvas(
          graphCanvas_, canvasOrigin, pointer);
      ImGui::OpenPopup("##MaterialGraphCanvasContext");
    }
  }
  if (ImGui::BeginPopup("##MaterialGraphCanvasContext")) {
    ImGui::TextDisabled("Add node at cursor");
    DrawMaterialGraphNodePalette(scene, source, graph, contextNodePosition);
    ImGui::EndPopup();
  }
  if (ImGui::BeginPopup("##MaterialGraphSelectionContext")) {
    if (!selectedGraphNodeId_.empty()) {
      const MaterialGraphNodeData *node =
          MATERIAL_GRAPH_UI::FindNode(graph, selectedGraphNodeId_);
      const bool isOutput = node != nullptr &&
          node->kind == MaterialNodeKind::SurfaceOutput;
      ImGui::BeginDisabled(isOutput);
      requestDeleteNode = ImGui::MenuItem("Delete Node", "Delete");
      ImGui::EndDisabled();
      if (node != nullptr) {
        const bool collapsed = collapsedGraphNodeIds_.contains(node->id);
        if (ImGui::MenuItem(collapsed ? "Expand Node" : "Collapse Node")) {
          if (collapsed) {
            collapsedGraphNodeIds_.erase(node->id);
          } else {
            collapsedGraphNodeIds_.insert(node->id);
          }
        }
      }
      if (isOutput) {
        ImGui::TextDisabled("Surface Output is the required graph root");
      }
    } else if (!selectedGraphEdgeId_.empty()) {
      requestDeleteEdge =
          ImGui::MenuItem("Delete Connection", "Delete");
    }
    ImGui::EndPopup();
  }

  ImGuiIO &io = ImGui::GetIO();
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
    const MaterialGraphNodeData *node =
        MATERIAL_GRAPH_UI::FindNode(graph, selectedGraphNodeId_);
    requestDeleteNode = node != nullptr &&
        node->kind != MaterialNodeKind::SurfaceOutput;
    requestDeleteEdge = !requestDeleteNode &&
        !selectedGraphEdgeId_.empty();
  }
  if (requestDeleteNode && !selectedGraphNodeId_.empty()) {
    const MaterialGraphNodeData *node =
        MATERIAL_GRAPH_UI::FindNode(graph, selectedGraphNodeId_);
    if (node == nullptr || node->kind == MaterialNodeKind::SurfaceOutput) {
      statusMessage_ = "Surface Output is the required graph root";
      requestDeleteNode = false;
    }
  }
  if (requestDeleteNode && !selectedGraphNodeId_.empty()) {
    MaterialAssetData before = source;
    const std::string deletedId = selectedGraphNodeId_;
    collapsedGraphNodeIds_.erase(deletedId);
    std::erase_if(graph.nodes, [&deletedId](const auto &node) {
      return node.id == deletedId;
    });
    std::erase_if(graph.edges, [&deletedId](const auto &edge) {
      return edge.sourceNodeId == deletedId ||
          edge.targetNodeId == deletedId;
    });
    selectedGraphNodeId_.clear();
    selectedGraphEdgeId_.clear();
    graphLinkDragActive_ = false;
    rewiredGraphEdgeId_.clear();
    draggedGraphNodeId_.clear();
    graphNodeDragStarted_ = false;
    CommitMaterialMutation(std::move(before),
                           "Delete material graph node", scene);
  } else if (requestDeleteEdge && !selectedGraphEdgeId_.empty()) {
    MaterialAssetData before = source;
    const std::string deletedId = selectedGraphEdgeId_;
    std::erase_if(graph.edges, [&deletedId](const auto &edge) {
      return edge.id == deletedId;
    });
    selectedGraphEdgeId_.clear();
    CommitMaterialMutation(std::move(before),
                           "Delete material graph edge", scene);
  }
#else
  (void)scene;
  (void)source;
  (void)graph;
  (void)completedTargetNode;
  (void)completedTargetPin;
  (void)linkReleased;
  (void)mouseInsideCanvas;
  (void)pointerOverNode;
  (void)hoveredNodeId;
  (void)hoveredEdgeId;
  (void)canvasOrigin;
  (void)pointer;
  (void)requestDeleteNode;
  (void)requestDeleteEdge;
#endif
}

} // namespace HIKARI::EDITOR
