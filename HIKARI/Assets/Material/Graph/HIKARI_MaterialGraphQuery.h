#pragma once

#include <algorithm>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "Assets/Material/Graph/HIKARI_MaterialGraphData.h"

namespace HIKARI {
inline std::unordered_set<std::string> CollectMaterialGraphUpstreamNodes(const MaterialGraphData& graph,
                                                                         std::string_view root = {}) {
    std::unordered_map<std::string, std::vector<std::string_view>> inputs;
    for (const auto& edge : graph.edges)
        inputs[edge.targetNodeId].push_back(edge.sourceNodeId);
    std::vector<std::string> pending{std::string(root.empty() ? graph.outputNodeId : root)};
    std::unordered_set<std::string> reachable;
    while (!pending.empty()) {
        std::string id = std::move(pending.back());
        pending.pop_back();
        if (id.empty() || !reachable.insert(id).second)
            continue;
        if (auto found = inputs.find(id); found != inputs.end())
            for (auto input : found->second)
                pending.emplace_back(input);
    }
    return reachable;
}

// Canonical material-graph lookup helpers. All graph consumers must use
// these instead of local find copies.

inline const MaterialGraphNodeData* FindMaterialGraphNode(const MaterialGraphData& graph,
                                                          std::string_view id) noexcept {
    const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                    [id](const MaterialGraphNodeData& node) { return node.id == id; });
    return found == graph.nodes.end() ? nullptr : &*found;
}

inline MaterialGraphNodeData* FindMaterialGraphNode(MaterialGraphData& graph, std::string_view id) noexcept {
    const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                    [id](const MaterialGraphNodeData& node) { return node.id == id; });
    return found == graph.nodes.end() ? nullptr : &*found;
}

inline const MaterialGraphParameterData* FindMaterialGraphParameter(const MaterialGraphData& graph,
                                                                    std::string_view id) noexcept {
    const auto found = std::find_if(graph.parameters.begin(), graph.parameters.end(),
                                    [id](const MaterialGraphParameterData& parameter) { return parameter.id == id; });
    return found == graph.parameters.end() ? nullptr : &*found;
}

inline MaterialGraphParameterData* FindMaterialGraphParameter(MaterialGraphData& graph, std::string_view id) noexcept {
    const auto found = std::find_if(graph.parameters.begin(), graph.parameters.end(),
                                    [id](const MaterialGraphParameterData& parameter) { return parameter.id == id; });
    return found == graph.parameters.end() ? nullptr : &*found;
}

inline const MaterialGraphEdgeData* FindMaterialGraphInputEdge(const MaterialGraphData& graph, std::string_view nodeId,
                                                               std::string_view pin) noexcept {
    const auto found =
        std::find_if(graph.edges.begin(), graph.edges.end(), [nodeId, pin](const MaterialGraphEdgeData& edge) {
            return edge.targetNodeId == nodeId && edge.targetPin == pin;
        });
    return found == graph.edges.end() ? nullptr : &*found;
}

inline const MaterialGraphResourceData* FindMaterialGraphResource(const MaterialGraphData& graph,
                                                                  std::string_view id) noexcept {
    const auto found = std::find_if(graph.resources.begin(), graph.resources.end(),
                                    [id](const MaterialGraphResourceData& resource) { return resource.id == id; });
    return found == graph.resources.end() ? nullptr : &*found;
}

inline MaterialGraphResourceData* FindMaterialGraphResource(MaterialGraphData& graph, std::string_view id) noexcept {
    const auto found = std::find_if(graph.resources.begin(), graph.resources.end(),
                                    [id](const MaterialGraphResourceData& resource) { return resource.id == id; });
    return found == graph.resources.end() ? nullptr : &*found;
}

} // namespace HIKARI
