#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
namespace HIKARI::EDITOR {
bool ModelMaterialWorkspaceController::CopySelectedGraphNodes(MaterialAssetData &source,
                                                              MaterialGraphData &graph) {
  (void)source;
  if (selectedGraphNodeIds_.empty() && !selectedGraphNodeId_.empty()) {
    selectedGraphNodeIds_.push_back(selectedGraphNodeId_);
  }
  if (selectedGraphNodeIds_.empty()) {
    return false;
  }
  graphClipboard_ = {};
  const std::unordered_set<std::string> selected(selectedGraphNodeIds_.begin(),
                                                 selectedGraphNodeIds_.end());
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
    if (node.kind == MaterialNodeKind::Parameter && !node.referenceId.empty()) {
      parameterIds.insert(node.referenceId);
    }
    if (node.kind == MaterialNodeKind::TextureSample && !node.referenceId.empty()) {
      resourceIds.insert(node.referenceId);
    }
  }
  for (const MaterialGraphParameterData &parameter : graph.parameters) {
    if (parameterIds.contains(parameter.id)) {
      graphClipboard_.parameters.push_back(parameter);
    }
  }
  for (const MaterialGraphResourceData &resource : graph.resources) {
    if (resourceIds.contains(resource.id)) {
      graphClipboard_.resources.push_back(resource);
    }
  }
  for (const MaterialGraphEdgeData &edge : graph.edges) {
    if (selected.contains(edge.sourceNodeId) && selected.contains(edge.targetNodeId)) {
      graphClipboard_.edges.push_back(edge);
    }
  }
  for (const auto &group : graph.groups) {
    auto copy = group;
    std::erase_if(copy.nodeIds, [&](const auto &id) { return !selected.contains(id); });
    if (!copy.nodeIds.empty())
      graphClipboard_.groups.push_back(std::move(copy));
  }
  return true;
}

bool ModelMaterialWorkspaceController::PasteGraphNodes(DocumentSceneBase &scene,
                                                       MaterialAssetData &source,
                                                       MaterialGraphData &graph,
                                                       const MATH::Vec2 &offset) {
  return PasteMaterialGraphFragment(scene, source, graph, graphClipboard_, offset, false,
                                    "Paste material graph nodes");
}

bool ModelMaterialWorkspaceController::PasteMaterialGraphFragment(
    DocumentSceneBase &scene, MaterialAssetData &source, MaterialGraphData &graph,
    const MaterialGraphData &fragment, MATH::Vec2 offset, bool arrange, std::string label) {
  if (fragment.nodes.empty())
    return false;
  MaterialAssetData before = source;
  std::unordered_map<std::string, std::string> nodeMap{};
  std::unordered_map<std::string, std::string> parameterMap{};
  std::unordered_map<std::string, std::string> resourceMap{};
  std::vector<std::string> pastedIds{};
  for (const MaterialGraphNodeData &clip : fragment.nodes) {
    // 出力とシングルトンを複製すると、全選択コピーだけで有効な材質が壊れる。
    if ((clip.kind == MaterialNodeKind::SurfaceOutput ||
         ASSETS::MATERIAL::IsSingletonReadyMadeMaterialNode(clip.kind)) &&
        std::any_of(graph.nodes.begin(), graph.nodes.end(),
                    [&](const auto &existing) { return existing.kind == clip.kind; }))
      continue;
    MaterialGraphNodeData node = clip;
    const std::string oldId = node.id;
    node.id = "Node_" + std::to_string(authoringIdCounter_++);
    node.position.x += offset.x;
    node.position.y += offset.y;
    if (node.kind == MaterialNodeKind::Parameter && !node.referenceId.empty()) {
      const std::string oldParameterId = node.referenceId;
      if (const auto mapped = parameterMap.find(oldParameterId); mapped != parameterMap.end()) {
        node.referenceId = mapped->second;
      } else {
        const std::string newParameterId = "parameter.paste_" + node.id;
        parameterMap[oldParameterId] = newParameterId;
        const auto sourceParameter =
            std::find_if(fragment.parameters.begin(), fragment.parameters.end(),
                         [&oldParameterId](const MaterialGraphParameterData &p) {
                           return p.id == oldParameterId;
                         });
        if (sourceParameter != fragment.parameters.end()) {
          MaterialGraphParameterData parameter = *sourceParameter;
          parameter.id = newParameterId;
          graph.parameters.push_back(std::move(parameter));
        }
        node.referenceId = newParameterId;
      }
    }
    if (node.kind == MaterialNodeKind::TextureSample && !node.referenceId.empty()) {
      const std::string oldResourceId = node.referenceId;
      if (const auto mapped = resourceMap.find(oldResourceId); mapped != resourceMap.end()) {
        node.referenceId = mapped->second;
      } else {
        const std::string newResourceId = "Texture_" + std::to_string(authoringIdCounter_++);
        resourceMap[oldResourceId] = newResourceId;
        const auto sourceResource = std::find_if(
            fragment.resources.begin(), fragment.resources.end(),
            [&oldResourceId](const MaterialGraphResourceData &r) { return r.id == oldResourceId; });
        if (sourceResource != fragment.resources.end()) {
          MaterialGraphResourceData resource = *sourceResource;
          resource.id = newResourceId;
          graph.resources.push_back(std::move(resource));
        }
        node.referenceId = newResourceId;
      }
    }
    nodeMap[oldId] = node.id;
    pastedIds.push_back(node.id);
    graph.nodes.push_back(std::move(node));
  }
  if (pastedIds.empty())
    return false;
  for (const MaterialGraphEdgeData &clip : fragment.edges) {
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
  for (const auto &group : fragment.groups) {
    auto copy = group;
    copy.id = "Group_" + std::to_string(authoringIdCounter_++);
    copy.nodeIds.clear();
    copy.position.x += offset.x;
    copy.position.y += offset.y;
    for (const auto &id : group.nodeIds)
      if (auto found = nodeMap.find(id); found != nodeMap.end())
        copy.nodeIds.push_back(found->second);
    if (!copy.nodeIds.empty())
      graph.groups.push_back(std::move(copy));
  }
  if (arrange)
    MATERIAL_GRAPH_UI::AutoLayoutMaterialGraph(graph, pastedIds);
  selectedGraphNodeIds_ = std::move(pastedIds);
  selectedGraphNodeId_ =
      selectedGraphNodeIds_.empty() ? std::string{} : selectedGraphNodeIds_.back();
  selectedGraphEdgeId_.clear();
  CommitMaterialMutation(std::move(before), std::move(label), scene);
  return true;
}

} // namespace HIKARI::EDITOR
