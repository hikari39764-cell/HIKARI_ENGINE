#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphDefaultInputs.h"

#include <algorithm>
#include <string>
#include <unordered_set>

#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {
namespace {

const MaterialGraphEdgeData *FindInputEdge(
    const MaterialGraphData &graph, std::string_view nodeId,
    std::string_view pin) noexcept {
  const auto found = std::find_if(
      graph.edges.begin(), graph.edges.end(),
      [nodeId, pin](const MaterialGraphEdgeData &edge) {
        return edge.targetNodeId == nodeId && edge.targetPin == pin;
      });
  return found != graph.edges.end() ? &*found : nullptr;
}

bool NodeDependsOnParameter(
    const MaterialGraphData &graph, std::string_view nodeId,
    std::string_view parameterId,
    std::unordered_set<std::string> &visited) {
  if (!visited.emplace(nodeId).second) {
    return false;
  }
  const MaterialGraphNodeData *node = FindNode(graph, nodeId);
  if (node == nullptr) {
    return false;
  }
  if (node->kind == MaterialNodeKind::Parameter) {
    return node->referenceId == parameterId;
  }
  for (const MaterialGraphEdgeData &edge : graph.edges) {
    if (edge.targetNodeId == node->id &&
        NodeDependsOnParameter(
            graph, edge.sourceNodeId, parameterId, visited)) {
      return true;
    }
  }
  return false;
}

bool NodeDependsOnResource(
    const MaterialGraphData &graph, std::string_view nodeId,
    std::string_view resourceId,
    std::unordered_set<std::string> &visited) {
  if (!visited.emplace(nodeId).second) {
    return false;
  }
  const MaterialGraphNodeData *node = FindNode(graph, nodeId);
  if (node == nullptr) {
    return false;
  }
  if (node->kind == MaterialNodeKind::TextureSample) {
    return node->referenceId == resourceId;
  }
  for (const MaterialGraphEdgeData &edge : graph.edges) {
    if (edge.targetNodeId == node->id &&
        NodeDependsOnResource(
            graph, edge.sourceNodeId, resourceId, visited)) {
      return true;
    }
  }
  return false;
}

} // namespace

bool InputUsesParameterDefault(
    const MaterialGraphData &graph, const MaterialGraphNodeData &node,
    std::string_view pin, std::string_view parameterId) {
  const MaterialGraphEdgeData *edge = FindInputEdge(graph, node.id, pin);
  if (edge == nullptr) {
    return true;
  }
  std::unordered_set<std::string> visited{};
  return NodeDependsOnParameter(
      graph, edge->sourceNodeId, parameterId, visited);
}

bool InputUsesResourceDefault(
    const MaterialGraphData &graph, const MaterialGraphNodeData &node,
    std::string_view pin, std::string_view resourceId) {
  const MaterialGraphEdgeData *edge = FindInputEdge(graph, node.id, pin);
  if (edge == nullptr) {
    return true;
  }
  std::unordered_set<std::string> visited{};
  return NodeDependsOnResource(
      graph, edge->sourceNodeId, resourceId, visited);
}

} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI
