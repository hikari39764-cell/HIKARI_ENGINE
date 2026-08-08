#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <utility>

#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

namespace {

void RunMaterialOperation(
    const EditorOperationRegistry *registry,
    const std::string &operationId,
    std::string &outStatusMessage) {
  if (registry == nullptr) {
    outStatusMessage = "material operation bus is not ready";
    return;
  }
  const EditorOperationResult result =
      registry->Execute(operationId);
  outStatusMessage = result.message;
}

} // namespace

bool ModelMaterialWorkspaceController::DrawMaterialGraphToolbar(
    DocumentSceneBase &scene, MaterialAssetData &source,
    MaterialGraphData &graph, bool &requestFrameAll,
    bool &requestFocusSelection, bool &requestDeleteNode,
    bool &requestDeleteEdge) {
#if defined(HIKARI_WITH_EDITOR)
  if (ImGui::Button(
          TEXT::Localize("ui.materialGraph.addNode").c_str())) {
    ImGui::OpenPopup("##MaterialGraphAddNode");
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!CanUndo());
  if (ImGui::Button(
          TEXT::Localize("ui.materialGraph.undo").c_str())) {
    RunMaterialOperation(
        operationRegistry_, "material.undo", statusMessage_);
    ImGui::EndDisabled();
    return true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!CanRedo());
  if (ImGui::Button(
          TEXT::Localize("ui.materialGraph.redo").c_str())) {
    RunMaterialOperation(
        operationRegistry_, "material.redo", statusMessage_);
    ImGui::EndDisabled();
    return true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  requestFrameAll = ImGui::Button(
      TEXT::Localize("ui.materialGraph.frameAll").c_str());
  ImGui::SameLine();
  ImGui::BeginDisabled(selectedGraphNodeId_.empty());
  requestFocusSelection = ImGui::Button(
      TEXT::Localize("ui.materialGraph.focusSelection").c_str());
  ImGui::EndDisabled();
  ImGui::SameLine();
  const bool hasNodeSelection =
      !selectedGraphNodeIds_.empty() ||
      !selectedGraphNodeId_.empty();
  ImGui::BeginDisabled(!hasNodeSelection);
  if (ImGui::Button(
          TEXT::Localize("ui.materialGraph.copy").c_str())) {
    (void)CopySelectedGraphNodes(source, graph);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(graphClipboard_.nodes.empty());
  if (ImGui::Button(
          TEXT::Localize("ui.materialGraph.paste").c_str())) {
    (void)PasteGraphNodes(
        scene, source, graph, MATH::Vec2{ 30.0f, 30.0f });
    return true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!hasNodeSelection);
  if (ImGui::Button(
          TEXT::Localize("ui.materialGraph.duplicate").c_str())) {
    RunMaterialOperation(
        operationRegistry_, "material.duplicate_selected",
        statusMessage_);
    return true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!hasNodeSelection);
  if (ImGui::Button(
          TEXT::Localize("ui.materialGraph.arrange").c_str())) {
    RunMaterialOperation(
        operationRegistry_, "material.arrange_selected",
        statusMessage_);
    return true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button(
          TEXT::Localize("ui.materialGraph.organize").c_str())) {
    ImGui::OpenPopup("##MaterialGraphOrganize");
  }
  if (ImGui::BeginPopup("##MaterialGraphOrganize")) {
    const bool hasSelection = !selectedGraphNodeId_.empty();
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::MenuItem(
            TEXT::Localize(
                "ui.materialGraph.collapseSelected").c_str())) {
      RunMaterialOperation(
          operationRegistry_, "material.collapse_selected",
          statusMessage_);
    }
    if (ImGui::MenuItem(
            TEXT::Localize(
                "ui.materialGraph.expandSelected").c_str())) {
      RunMaterialOperation(
          operationRegistry_, "material.expand_selected",
          statusMessage_);
    }
    ImGui::Separator();
    ImGui::BeginDisabled(!hasSelection);
    if (ImGui::MenuItem(
            TEXT::Localize(
                "ui.materialGraph.groupSelection").c_str())) {
      RunMaterialOperation(
          operationRegistry_, "material.group_selected",
          statusMessage_);
    }
    if (ImGui::MenuItem(
            TEXT::Localize(
                "ui.materialGraph.ungroupSelected").c_str())) {
      RunMaterialOperation(
          operationRegistry_, "material.ungroup_selected",
          statusMessage_);
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (ImGui::MenuItem(
            TEXT::Localize("ui.materialGraph.collapseAll").c_str())) {
      RunMaterialOperation(
          operationRegistry_, "material.collapse_all",
          statusMessage_);
    }
    if (ImGui::MenuItem(
            TEXT::Localize("ui.materialGraph.expandAll").c_str())) {
      RunMaterialOperation(
          operationRegistry_, "material.expand_all",
          statusMessage_);
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(
      !hasNodeSelection && selectedGraphEdgeId_.empty());
  if (ImGui::Button(
          TEXT::Localize(
              "ui.materialGraph.deleteSelection").c_str())) {
    requestDeleteNode = hasNodeSelection;
    requestDeleteEdge = !hasNodeSelection &&
        !selectedGraphEdgeId_.empty();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled(
      "%s",
      TEXT::Localize("ui.materialGraph.dragHint").c_str());
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
      SetGraphNodeSelection(
          hoveredNodeId,
          ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift);
      selectedGraphEdgeId_.clear();
      ImGui::OpenPopup("##MaterialGraphSelectionContext");
    } else if (!hoveredEdgeId.empty()) {
      ClearGraphNodeSelection();
      selectedGraphEdgeId_ = hoveredEdgeId;
      ImGui::OpenPopup("##MaterialGraphSelectionContext");
    } else {
      ClearGraphNodeSelection();
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
      if (ImGui::MenuItem(
              TEXT::Localize("ui.materialGraph.copy").c_str(),
              "Ctrl+C")) {
        (void)CopySelectedGraphNodes(source, graph);
      }
      if (ImGui::MenuItem(
              TEXT::Localize(
                  "ui.materialGraph.duplicate").c_str(),
              "Ctrl+D")) {
        RunMaterialOperation(
            operationRegistry_, "material.duplicate_selected",
            statusMessage_);
        ImGui::EndPopup();
        return;
      }
      if (ImGui::MenuItem(
              TEXT::Localize(
                  "ui.materialGraph.arrangeSelected").c_str())) {
        RunMaterialOperation(
            operationRegistry_, "material.arrange_selected",
            statusMessage_);
        ImGui::EndPopup();
        return;
      }
      ImGui::Separator();
      const MaterialGraphNodeData *node =
          MATERIAL_GRAPH_UI::FindNode(graph, selectedGraphNodeId_);
      const bool isOutput = node != nullptr &&
          node->kind == MaterialNodeKind::SurfaceOutput;
      const bool multiSelection =
          selectedGraphNodeIds_.size() > 1u;
      ImGui::BeginDisabled(isOutput);
      requestDeleteNode = ImGui::MenuItem(
          TEXT::Localize(
              multiSelection
                  ? "ui.materialGraph.deleteSelected"
                  : "ui.materialGraph.deleteNode")
              .c_str(),
          "Delete");
      ImGui::EndDisabled();
      if (node != nullptr) {
        const bool collapsed = collapsedGraphNodeIds_.contains(node->id);
        if (ImGui::MenuItem(
                TEXT::Localize(
                    collapsed
                        ? "ui.materialGraph.expandNode"
                        : "ui.materialGraph.collapseNode")
                    .c_str())) {
          RunMaterialOperation(
              operationRegistry_,
              collapsed ? "material.expand_selected"
                        : "material.collapse_selected",
              statusMessage_);
        }
      }
      if (isOutput) {
        ImGui::TextDisabled(
            "%s",
            TEXT::Localize(
                "ui.materialGraph.surfaceOutputRoot").c_str());
      }
    } else if (!selectedGraphEdgeId_.empty()) {
      requestDeleteEdge =
          ImGui::MenuItem(
              TEXT::Localize(
                  "ui.materialGraph.deleteConnection").c_str(),
              "Delete");
    }
    ImGui::EndPopup();
  }

  ImGuiIO &io = ImGui::GetIO();
  const bool hasNodeSelection =
      !selectedGraphNodeIds_.empty() ||
      !selectedGraphNodeId_.empty();
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      !io.WantTextInput) {
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
      (void)CopySelectedGraphNodes(source, graph);
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
      (void)PasteGraphNodes(
          scene, source, graph, MATH::Vec2{ 30.0f, 30.0f });
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D)) {
      RunMaterialOperation(
          operationRegistry_, "material.duplicate_selected",
          statusMessage_);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
      requestDeleteNode = hasNodeSelection;
      requestDeleteEdge = !requestDeleteNode &&
          !selectedGraphEdgeId_.empty();
    }
  }
  if (requestDeleteNode) {
    RunMaterialOperation(
        operationRegistry_, "material.delete_selected",
        statusMessage_);
    graphLinkDragActive_ = false;
    rewiredGraphEdgeId_.clear();
    draggedGraphNodeId_.clear();
    graphNodeDragStarted_ = false;
    requestDeleteNode = false;
  } else if (requestDeleteEdge && !selectedGraphEdgeId_.empty()) {
    RunMaterialOperation(
        operationRegistry_, "material.delete_connection",
        statusMessage_);
    requestDeleteEdge = false;
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
