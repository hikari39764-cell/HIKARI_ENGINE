#pragma once

#include <algorithm>
#include <string_view>

#include "Assets/Material/Graph/HIKARI_MaterialGraphData.h"

namespace HIKARI {

    // Canonical material-graph lookup helpers. All graph consumers must use
    // these instead of local find copies.

    inline const MaterialGraphNodeData* FindMaterialGraphNode(
        const MaterialGraphData& graph,
        std::string_view id) noexcept {
        const auto found = std::find_if(
            graph.nodes.begin(), graph.nodes.end(),
            [id](const MaterialGraphNodeData& node) {
                return node.id == id;
            });
        return found == graph.nodes.end() ? nullptr : &*found;
    }

    inline MaterialGraphNodeData* FindMaterialGraphNode(
        MaterialGraphData& graph,
        std::string_view id) noexcept {
        const auto found = std::find_if(
            graph.nodes.begin(), graph.nodes.end(),
            [id](const MaterialGraphNodeData& node) {
                return node.id == id;
            });
        return found == graph.nodes.end() ? nullptr : &*found;
    }

    inline const MaterialGraphParameterData* FindMaterialGraphParameter(
        const MaterialGraphData& graph,
        std::string_view id) noexcept {
        const auto found = std::find_if(
            graph.parameters.begin(), graph.parameters.end(),
            [id](const MaterialGraphParameterData& parameter) {
                return parameter.id == id;
            });
        return found == graph.parameters.end() ? nullptr : &*found;
    }

    inline MaterialGraphParameterData* FindMaterialGraphParameter(
        MaterialGraphData& graph,
        std::string_view id) noexcept {
        const auto found = std::find_if(
            graph.parameters.begin(), graph.parameters.end(),
            [id](const MaterialGraphParameterData& parameter) {
                return parameter.id == id;
            });
        return found == graph.parameters.end() ? nullptr : &*found;
    }

    inline const MaterialGraphEdgeData* FindMaterialGraphInputEdge(
        const MaterialGraphData& graph,
        std::string_view nodeId,
        std::string_view pin) noexcept {
        const auto found = std::find_if(
            graph.edges.begin(), graph.edges.end(),
            [nodeId, pin](const MaterialGraphEdgeData& edge) {
                return edge.targetNodeId == nodeId &&
                    edge.targetPin == pin;
            });
        return found == graph.edges.end() ? nullptr : &*found;
    }

    inline const MaterialGraphResourceData* FindMaterialGraphResource(
        const MaterialGraphData& graph,
        std::string_view id) noexcept {
        const auto found = std::find_if(
            graph.resources.begin(), graph.resources.end(),
            [id](const MaterialGraphResourceData& resource) {
                return resource.id == id;
            });
        return found == graph.resources.end() ? nullptr : &*found;
    }

    inline MaterialGraphResourceData* FindMaterialGraphResource(
        MaterialGraphData& graph,
        std::string_view id) noexcept {
        const auto found = std::find_if(
            graph.resources.begin(), graph.resources.end(),
            [id](const MaterialGraphResourceData& resource) {
                return resource.id == id;
            });
        return found == graph.resources.end() ? nullptr : &*found;
    }

} // namespace HIKARI
