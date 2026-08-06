#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

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
  selectedGraphNodeId_ = id;
  selectedGraphEdgeId_.clear();
}

void ModelMaterialWorkspaceController::ClearGraphNodeSelection() {
  selectedGraphNodeIds_.clear();
  selectedGraphNodeId_.clear();
}

bool ModelMaterialWorkspaceController::CopySelectedGraphNodes(
    MaterialAssetData &source, MaterialGraphData &graph) {
  if (selectedGraphNodeIds_.empty() &&
      !selectedGraphNodeId_.empty()) {
    selectedGraphNodeIds_.push_back(selectedGraphNodeId_);
  }
  if (selectedGraphNodeIds_.empty()) {
    return false;
  }
  graphClipboard_ = {};
  const std::unordered_set<std::string> selected(
      selectedGraphNodeIds_.begin(), selectedGraphNodeIds_.end());
  for (const MaterialGraphNodeData &node : graph.nodes) {
    if (selected.contains(node.id)) {
      graphClipboard_.nodes.push_back(node);
    }
  }
  if (graphClipboard_.nodes.empty()) {
    return false;
  }
  std::unordered_set<std::string> parameterIds{};
  std::unordered_set<std::string> resourceIds{};
  for (const MaterialGraphNodeData &node : graphClipboard_.nodes) {
    if (node.kind == MaterialNodeKind::Parameter &&
        !node.referenceId.empty()) {
      parameterIds.insert(node.referenceId);
    }
    if (node.kind == MaterialNodeKind::TextureSample &&
        !node.referenceId.empty()) {
      resourceIds.insert(node.referenceId);
    }
  }
  for (const MaterialGraphParameterData &parameter :
       graph.parameters) {
    if (parameterIds.contains(parameter.id)) {
      graphClipboard_.parameters.push_back(parameter);
    }
  }
  for (const MaterialGraphResourceData &resource :
       graph.resources) {
    if (resourceIds.contains(resource.id)) {
      graphClipboard_.resources.push_back(resource);
    }
  }
  for (const MaterialGraphEdgeData &edge : graph.edges) {
    if (selected.contains(edge.sourceNodeId) &&
        selected.contains(edge.targetNodeId)) {
      graphClipboard_.edges.push_back(edge);
    }
  }
  return true;
}

bool ModelMaterialWorkspaceController::PasteGraphNodes(
    DocumentSceneBase &scene,
    MaterialAssetData &source, MaterialGraphData &graph,
    const MATH::Vec2 &offset) {
  if (graphClipboard_.nodes.empty()) {
    return false;
  }
  MaterialAssetData before = source;
  std::unordered_map<std::string, std::string> nodeMap{};
  std::unordered_map<std::string, std::string> parameterMap{};
  std::unordered_map<std::string, std::string> resourceMap{};
  std::vector<std::string> pastedIds{};
  for (const MaterialGraphNodeData &clip : graphClipboard_.nodes) {
    MaterialGraphNodeData node = clip;
    const std::string oldId = node.id;
    node.id = "Node_" + std::to_string(authoringIdCounter_++);
    node.position.x += offset.x;
    node.position.y += offset.y;
    if (node.kind == MaterialNodeKind::Parameter &&
        !node.referenceId.empty()) {
      const std::string oldParameterId = node.referenceId;
      const std::string newParameterId =
          "parameter.paste_" + node.id;
      parameterMap[oldParameterId] = newParameterId;
      const auto sourceParameter = std::find_if(
          graphClipboard_.parameters.begin(),
          graphClipboard_.parameters.end(),
          [&oldParameterId](const MaterialGraphParameterData &p) {
            return p.id == oldParameterId;
          });
      if (sourceParameter != graphClipboard_.parameters.end()) {
        MaterialGraphParameterData parameter = *sourceParameter;
        parameter.id = newParameterId;
        graph.parameters.push_back(std::move(parameter));
      }
      node.referenceId = newParameterId;
    }
    if (node.kind == MaterialNodeKind::TextureSample &&
        !node.referenceId.empty()) {
      const std::string oldResourceId = node.referenceId;
      const std::string newResourceId =
          "Texture_" + std::to_string(authoringIdCounter_++);
      resourceMap[oldResourceId] = newResourceId;
      const auto sourceResource = std::find_if(
          graphClipboard_.resources.begin(),
          graphClipboard_.resources.end(),
          [&oldResourceId](const MaterialGraphResourceData &r) {
            return r.id == oldResourceId;
          });
      if (sourceResource != graphClipboard_.resources.end()) {
        MaterialGraphResourceData resource = *sourceResource;
        resource.id = newResourceId;
        graph.resources.push_back(std::move(resource));
      }
      node.referenceId = newResourceId;
    }
    nodeMap[oldId] = node.id;
    pastedIds.push_back(node.id);
    graph.nodes.push_back(std::move(node));
  }
  for (const MaterialGraphEdgeData &clip : graphClipboard_.edges) {
    MaterialGraphEdgeData edge = clip;
    const auto sourceIt = nodeMap.find(edge.sourceNodeId);
    const auto targetIt = nodeMap.find(edge.targetNodeId);
    if (sourceIt == nodeMap.end() || targetIt == nodeMap.end()) {
      continue;
    }
    edge.id = "Edge_" + std::to_string(authoringIdCounter_++);
    edge.sourceNodeId = sourceIt->second;
    edge.targetNodeId = targetIt->second;
    graph.edges.push_back(std::move(edge));
  }
  selectedGraphNodeIds_ = std::move(pastedIds);
  selectedGraphNodeId_ = selectedGraphNodeIds_.empty()
      ? std::string{}
      : selectedGraphNodeIds_.back();
  selectedGraphEdgeId_.clear();
  CommitMaterialMutation(
      std::move(before), "Paste material graph nodes", scene);
  return true;
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
  std::erase_if(
      graph.edges, [&selected](const MaterialGraphEdgeData &edge) {
        return selected.contains(edge.sourceNodeId) ||
            selected.contains(edge.targetNodeId);
      });
  CommitMaterialMutation(
      std::move(before), "Delete material graph nodes", scene);
  selectedGraphNodeIds_.clear();
  selectedGraphNodeId_.clear();
  selectedGraphEdgeId_.clear();
  return true;
}

bool ModelMaterialWorkspaceController::ArrangeSelectedGraphNodes(
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
  std::vector<MaterialGraphNodeData *> selected{};
  for (MaterialGraphNodeData &node : graph.nodes) {
    if (IsGraphNodeSelected(node.id)) {
      selected.push_back(&node);
    }
  }
  std::sort(
      selected.begin(), selected.end(),
      [](const MaterialGraphNodeData *lhs,
         const MaterialGraphNodeData *rhs) {
        return lhs->displayName < rhs->displayName;
      });
  constexpr float kColumnSpacing = 260.0f;
  constexpr float kRowSpacing = 220.0f;
  size_t column = 0u;
  size_t row = 0u;
  for (MaterialGraphNodeData *node : selected) {
    node->position = {
      40.0f + static_cast<float>(column) * kColumnSpacing,
      40.0f + static_cast<float>(row) * kRowSpacing,
    };
    ++column;
    if (column >= 4u) {
      column = 0u;
      ++row;
    }
  }
  CommitMaterialMutation(
      std::move(before), "Arrange selected material graph nodes", scene);
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
    maxY = (std::max)(maxY, node.position.y + 120.0f);
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

} // namespace HIKARI::EDITOR
