#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
#include "Assets/Material/Graph/HIKARI_MaterialNodeDefinition.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Editor/NodeGraph/HIKARI_NodeGraphCanvas.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialNodeQuery.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Runtime/Binding/Adapters/HIKARI_RuntimeMaterialValueAdapter.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::IsGraphNodeSelected(
    const std::string &id) const {
  return std::find(
      selectedGraphNodeIds_.begin(),
      selectedGraphNodeIds_.end(),
      id) != selectedGraphNodeIds_.end();
}

void ModelMaterialWorkspaceController::SetGraphNodeSelection(
    const std::string &id, bool additive) {
  if (!additive) {
    selectedGraphNodeIds_.clear();
  }
  const auto found = std::find(
      selectedGraphNodeIds_.begin(),
      selectedGraphNodeIds_.end(),
      id);
  if (additive && found != selectedGraphNodeIds_.end()) {
    selectedGraphNodeIds_.erase(found);
  } else if (found == selectedGraphNodeIds_.end()) {
    selectedGraphNodeIds_.push_back(id);
  }
  selectedGraphNodeId_ = selectedGraphNodeIds_.empty() ? std::string{} : selectedGraphNodeIds_.back();
  selectedGraphEdgeId_.clear();
}

void ModelMaterialWorkspaceController::ClearGraphNodeSelection() {
  selectedGraphNodeIds_.clear();
  selectedGraphNodeId_.clear();
}

bool ModelMaterialWorkspaceController::DuplicateSelectedGraphNodes(
    DocumentSceneBase &scene,
    MaterialAssetData &source, MaterialGraphData &graph) {
  if (!CopySelectedGraphNodes(source, graph)) {
    return false;
  }
  return PasteGraphNodes(
      scene, source, graph, MATH::Vec2{ 30.0f, 30.0f });
}

bool ModelMaterialWorkspaceController::DeleteSelectedGraphNodes(
    DocumentSceneBase &scene,
    MaterialAssetData &source, MaterialGraphData &graph) {
  if (selectedGraphNodeIds_.empty() &&
      !selectedGraphNodeId_.empty()) {
    selectedGraphNodeIds_.push_back(selectedGraphNodeId_);
  }
  if (selectedGraphNodeIds_.empty()) {
    return false;
  }
  MaterialAssetData before = source;
  const std::unordered_set<std::string> selected(
      selectedGraphNodeIds_.begin(), selectedGraphNodeIds_.end());
  const bool hasDeletableNode = std::any_of(
      graph.nodes.begin(), graph.nodes.end(),
      [&selected](const MaterialGraphNodeData &node) {
        return selected.contains(node.id) &&
            node.kind != MaterialNodeKind::SurfaceOutput;
      });
  if (!hasDeletableNode) {
    return false;
  }
  std::erase_if(
      graph.nodes, [&selected](const MaterialGraphNodeData &node) {
        return selected.contains(node.id) &&
            node.kind != MaterialNodeKind::SurfaceOutput;
      });
  std::unordered_set<std::string> remaining;
  for (const auto& node : graph.nodes) remaining.insert(node.id);
  std::erase_if(graph.edges, [&](const auto& edge) {
    return !remaining.contains(edge.sourceNodeId) || !remaining.contains(edge.targetNodeId);
  });
  for (auto& group : graph.groups) {
    std::erase_if(group.nodeIds, [&](const auto& id) { return !remaining.contains(id); });
  }
  std::erase_if(graph.groups, [](const auto& group) { return group.nodeIds.empty(); });
  CommitMaterialMutation(
      std::move(before), "Delete material graph nodes", scene);
  for (const std::string &id : selected) {
    collapsedGraphNodeIds_.erase(id);
  }
  selectedGraphNodeIds_.clear();
  selectedGraphNodeId_.clear();
  selectedGraphEdgeId_.clear();
  return true;
}

bool ModelMaterialWorkspaceController::CreateGraphGroupFromSelection(
    DocumentSceneBase &scene,
    MaterialAssetData &source, MaterialGraphData &graph) {
  if (selectedGraphNodeIds_.empty() &&
      !selectedGraphNodeId_.empty()) {
    selectedGraphNodeIds_.push_back(selectedGraphNodeId_);
  }
  if (selectedGraphNodeIds_.empty()) {
    return false;
  }
  MaterialAssetData before = source;
  const std::unordered_set<std::string> selected(
      selectedGraphNodeIds_.begin(), selectedGraphNodeIds_.end());
  float minX = (std::numeric_limits<float>::max)();
  float minY = (std::numeric_limits<float>::max)();
  float maxX = -(std::numeric_limits<float>::max)();
  float maxY = -(std::numeric_limits<float>::max)();
  bool hasNode = false;
  for (const MaterialGraphNodeData &node : graph.nodes) {
    if (!selected.contains(node.id)) {
      continue;
    }
    minX = (std::min)(minX, node.position.x);
    minY = (std::min)(minY, node.position.y);
    maxX = (std::max)(
        maxX, node.position.x + MATERIAL_GRAPH_UI::kNodeWidth);
    maxY = (std::max)(maxY, node.position.y + MATERIAL_GRAPH_UI::NodeLogicalHeight(graph, node, false));
    hasNode = true;
  }
  if (!hasNode) {
    return false;
  }
  const uint64_t groupNumber = authoringIdCounter_++;
  MaterialGraphGroupData group{};
  group.id = "Group_" + std::to_string(groupNumber);
  group.name = "Group " + std::to_string(groupNumber);
  group.position = { minX - 20.0f, minY - 40.0f };
  group.size = {
      (maxX - minX) + 40.0f,
      (maxY - minY) + 40.0f };
  group.nodeIds.assign(
      selectedGraphNodeIds_.begin(), selectedGraphNodeIds_.end());
  graph.groups.push_back(std::move(group));
  CommitMaterialMutation(
      std::move(before), "Create material graph group", scene);
  return true;
}

bool ModelMaterialWorkspaceController::UngroupSelectedGraphNodes(
    DocumentSceneBase &scene,
    MaterialAssetData &source, MaterialGraphData &graph) {
  if (selectedGraphNodeIds_.empty() &&
      !selectedGraphNodeId_.empty()) {
    selectedGraphNodeIds_.push_back(selectedGraphNodeId_);
  }
  if (selectedGraphNodeIds_.empty()) {
    return false;
  }
  MaterialAssetData before = source;
  const std::unordered_set<std::string> selected(
      selectedGraphNodeIds_.begin(), selectedGraphNodeIds_.end());
  std::erase_if(
      graph.groups, [&selected](const MaterialGraphGroupData &group) {
        return std::any_of(
            group.nodeIds.begin(), group.nodeIds.end(),
            [&selected](const std::string &nodeId) {
              return selected.contains(nodeId);
            });
      });
  CommitMaterialMutation(
      std::move(before), "Ungroup material graph nodes", scene);
  return true;
}

void ModelMaterialWorkspaceController::SetOperationRegistry(
    const EditorOperationRegistry *registry) noexcept {
  operationRegistry_ = registry;
}

bool ModelMaterialWorkspaceController::HasMaterialDocument()
    const noexcept {
  return authoringDocument_.HasMaterial();
}

bool ModelMaterialWorkspaceController::HasUnsavedMaterialChanges()
    const noexcept {
  return authoringDocument_.IsMaterialDirty() ||
         authoringDocument_.IsAbPreviewActive();
}

bool ModelMaterialWorkspaceController::DuplicateSelectedGraphNodes(
    DocumentSceneBase &scene, std::string &outMessage) {
  if (!HasMaterialDocument()) {
    outMessage = "未打开材质文档";
    return false;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    return false;
  }
  return DuplicateSelectedGraphNodes(scene, source, *graph);
}

bool ModelMaterialWorkspaceController::DeleteSelectedGraphNodes(
    DocumentSceneBase &scene, std::string &outMessage) {
  if (!HasMaterialDocument()) {
    outMessage = "未打开材质文档";
    return false;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    return false;
  }
  if (!DeleteSelectedGraphNodes(scene, source, *graph)) {
    outMessage = "没有可删除的选中节点（Surface Output 不能删除）";
    return false;
  }
  outMessage = "已删除选中节点";
  return true;
}

bool ModelMaterialWorkspaceController::DeleteSelectedGraphEdge(
    DocumentSceneBase &scene, std::string &outMessage) {
  if (!HasMaterialDocument() || selectedGraphEdgeId_.empty()) {
    outMessage = "没有选中的连线";
    return false;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    return false;
  }
  MaterialAssetData before = source;
  const std::string deletedId = selectedGraphEdgeId_;
  std::erase_if(
      graph->edges, [&deletedId](const MaterialGraphEdgeData &edge) {
        return edge.id == deletedId;
      });
  selectedGraphEdgeId_.clear();
  CommitMaterialMutation(
      std::move(before), "Delete material graph edge", scene);
  outMessage = "已删除选中连线";
  return true;
}

bool ModelMaterialWorkspaceController::CreateGraphGroupFromSelection(
    DocumentSceneBase &scene, std::string &outMessage) {
  if (!HasMaterialDocument()) {
    outMessage = "未打开材质文档";
    return false;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    return false;
  }
  if (!CreateGraphGroupFromSelection(scene, source, *graph)) {
    outMessage = "没有可分组的选中节点";
    return false;
  }
  outMessage = "已创建节点分组";
  return true;
}

bool ModelMaterialWorkspaceController::UngroupSelectedGraphNodes(
    DocumentSceneBase &scene, std::string &outMessage) {
  if (!HasMaterialDocument()) {
    outMessage = "未打开材质文档";
    return false;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    return false;
  }
  if (!UngroupSelectedGraphNodes(scene, source, *graph)) {
    outMessage = "没有可取消分组的选中节点";
    return false;
  }
  outMessage = "已取消选中节点分组";
  return true;
}

bool ModelMaterialWorkspaceController::ArrangeSelectedGraphNodes(
    DocumentSceneBase &scene, std::string &outMessage) {
  std::string code;
  return LayoutMaterialGraph(scene, outMessage, code, true);
}

bool ModelMaterialWorkspaceController::LayoutMaterialGraph(
    DocumentSceneBase& scene, std::string& outMessage, std::string& outCode,
    bool selectionOnly) {
  (void)scene;
  std::lock_guard<std::recursive_mutex> guard(authoringDocument_.GetMutex());
  outCode = "OK";
  auto& material = authoringDocument_.EditMaterial();
  auto* graph = MATERIAL_GRAPH_UI::EditableGraph(material);
  if (!authoringDocument_.HasMaterial() || graph == nullptr || graph->nodes.empty()) {
    outCode = "GRAPH_EMPTY";
    outMessage = TEXT::Localize("material.layout.empty");
    return false;
  }
  std::vector<std::string> selection;
  if (selectionOnly) {
    selection = selectedGraphNodeIds_;
    if (selection.empty() && !selectedGraphNodeId_.empty()) selection.push_back(selectedGraphNodeId_);
    if (selection.empty()) {
      outCode = "SELECTION_EMPTY";
      outMessage = TEXT::Localize("material.layout.selectionEmpty");
      return false;
    }
  }
  MaterialAssetData before = material;
  const bool changed = MATERIAL_GRAPH_UI::AutoLayoutMaterialGraph(*graph, selection);
  if (changed) {
    authoringDocument_.CommitMaterial(std::move(before), "Arrange material graph");
  }
  graphFrameRequested_ = !selectionOnly;
  outMessage = TEXT::Localize(changed ? "material.layout.done" : "material.layout.unchanged");
  return true;
}

void ModelMaterialWorkspaceController::FrameMaterialGraph(
    bool selection, const MATH::Vec2 &viewportSize) noexcept {
  if (!authoringDocument_.HasMaterial()) {
    return;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr || graph->nodes.empty()) {
    return;
  }
  float minX = (std::numeric_limits<float>::max)(), minY = minX;
  float maxX = -minX, maxY = -minX;
  bool found = false;
  for (const auto& node : graph->nodes) {
    if (selection && !IsGraphNodeSelected(node.id) && node.id != selectedGraphNodeId_) continue;
    const auto size = MATERIAL_GRAPH_UI::NodeLayoutSize(*graph, node);
    minX = (std::min)(minX,node.position.x); minY = (std::min)(minY,node.position.y);
    maxX = (std::max)(maxX,node.position.x+size.x); maxY = (std::max)(maxY,node.position.y+size.y);
    found = true;
  }
  if (found) NodeGraphCanvas::FrameBounds(graphCanvas_,viewportSize,{minX,minY},{maxX,maxY},
      80.0f,MATERIAL_GRAPH_UI::kCanvasMinimumZoom, selection ? 1.0f : MATERIAL_GRAPH_UI::kCanvasMaximumZoom);
}

void ModelMaterialWorkspaceController::SetSelectedGraphNodesCollapsed(
    bool collapsed) noexcept {
  if (!selectedGraphNodeIds_.empty()) {
    for (const std::string &id : selectedGraphNodeIds_) {
      if (collapsed) {
        collapsedGraphNodeIds_.insert(id);
      } else {
        collapsedGraphNodeIds_.erase(id);
      }
    }
  } else if (!selectedGraphNodeId_.empty()) {
    if (collapsed) {
      collapsedGraphNodeIds_.insert(selectedGraphNodeId_);
    } else {
      collapsedGraphNodeIds_.erase(selectedGraphNodeId_);
    }
  }
}

void ModelMaterialWorkspaceController::SetAllGraphNodesCollapsed(
    bool collapsed) noexcept {
  if (!authoringDocument_.HasMaterial()) {
    return;
  }
  MaterialAssetData &source = authoringDocument_.EditMaterial();
  MaterialGraphData *graph =
      MATERIAL_GRAPH_UI::EditableGraph(source);
  if (graph == nullptr) {
    return;
  }
  if (collapsed) {
    for (const MaterialGraphNodeData &node : graph->nodes) {
      collapsedGraphNodeIds_.insert(node.id);
    }
  } else {
    collapsedGraphNodeIds_.clear();
  }
}

} // namespace HIKARI::EDITOR
