#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <string_view>

namespace HIKARI::EDITOR {
namespace {

uint64_t TrailingNumber(std::string_view id) {
    size_t start = id.size();
    while (start > 0u && id[start - 1u] >= '0' && id[start - 1u] <= '9') {
        --start;
    }
    if (start == id.size()) {
        return 0u;
    }
    uint64_t value = 0u;
    for (size_t index = start; index < id.size(); ++index) {
        value = value * 10u + static_cast<uint64_t>(id[index] - '0');
        if (value > 1000000000u) {
            return 1000000000u;
        }
    }
    return value;
}

} // namespace

void ModelMaterialWorkspaceController::RefreshAuthoringIdCounter() {
    if (!authoringDocument_.HasMaterial()) {
        authoringIdCounter_ = 1u;
        return;
    }
    const MaterialAssetData& material = authoringDocument_.GetMaterial();
    const MaterialGraphData& graph =
        material.kind == MaterialAssetKind::Function ? material.function.graph : material.graph;
    uint64_t maxId = 0u;
    const auto consider = [&maxId](std::string_view id) { maxId = (std::max)(maxId, TrailingNumber(id)); };
    for (const MaterialGraphNodeData& node : graph.nodes) {
        consider(node.id);
    }
    for (const MaterialGraphEdgeData& edge : graph.edges) {
        consider(edge.id);
    }
    for (const MaterialGraphParameterData& parameter : graph.parameters) {
        consider(parameter.id);
    }
    for (const MaterialGraphResourceData& resource : graph.resources) {
        consider(resource.id);
    }
    for (const auto& group : graph.groups)
        consider(group.id);
    consider(graph.outputNodeId);
    authoringIdCounter_ = maxId + 1u;
}

} // namespace HIKARI::EDITOR
