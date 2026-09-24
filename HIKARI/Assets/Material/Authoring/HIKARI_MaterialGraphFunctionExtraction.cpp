#include "Assets/Material/Authoring/HIKARI_MaterialGraphReuse.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphQuery.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include <algorithm>
#include <map>
#include <set>

namespace HIKARI::ASSETS::MATERIAL {
bool ExtractMaterialGraphFunction(const MaterialAssetData& source, const std::vector<std::string>& selection,
                                  const AssetGuid& functionGuid, std::string_view name, std::string_view callId,
                                  MaterialAssetData& function, MaterialAssetData& replaced, std::string& error) {
    if (source.kind != MaterialAssetKind::Material && source.kind != MaterialAssetKind::Function) {
        error = "material.function.select";
        return false;
    }
    const auto& graph = source.kind == MaterialAssetKind::Function ? source.function.graph : source.graph;
    const std::set<std::string> selected(selection.begin(), selection.end());
    if (selected.empty() || FindMaterialGraphNode(graph, callId) || !functionGuid.IsValid()) {
        error = "material.function.select";
        return false;
    }
    for (const auto& id : selected)
        if (!FindMaterialGraphNode(graph, id)) {
            error = "material.function.select";
            return false;
        }
    std::set<std::string> roots;
    for (const auto& edge : graph.edges)
        if (selected.contains(edge.sourceNodeId) && !selected.contains(edge.targetNodeId))
            roots.insert(edge.sourceNodeId);
    if (roots.empty()) {
        roots = selected;
        for (const auto& edge : graph.edges)
            if (selected.contains(edge.targetNodeId))
                roots.erase(edge.sourceNodeId);
    }
    if (roots.size() != 1) {
        error = "material.function.oneOutput";
        return false;
    }
    const auto* outputNode = FindMaterialGraphNode(graph, *roots.begin());
    const auto outputPins = GetMaterialGraphNodeOutputPins(*outputNode);
    if (outputPins.empty()) {
        error = "material.function.oneOutput";
        return false;
    }
    const auto reachable = CollectMaterialGraphUpstreamNodes(graph, outputNode->id);
    for (const auto& id : selected)
        if (!reachable.contains(id)) {
            error = "material.function.connected";
            return false;
        }
    function = {};
    function.kind = MaterialAssetKind::Function;
    function.domain = source.domain;
    function.templateId = MaterialTemplateId::Custom;
    function.materialName = name;
    function.function.graph = graph;
    auto& extracted = function.function.graph;
    extracted.outputNodeId = outputNode->id;
    extracted.groups.clear();
    std::erase_if(extracted.nodes, [&](const auto& node) { return !selected.contains(node.id); });
    std::erase_if(extracted.edges, [&](const auto& edge) {
        return !selected.contains(edge.sourceNodeId) || !selected.contains(edge.targetNodeId);
    });
    for (const auto pin : outputPins) {
        const auto type = ResolveMaterialGraphPinType(graph, *outputNode, pin, true);
        if (!type) {
            error = "material.function.oneOutput";
            return false;
        }
        MaterialGraphParameterData p;
        p.id = pin;
        p.displayName = pin;
        p.defaultValue.type = *type;
        function.function.outputs.push_back(p);
    }
    replaced = source;
    auto& target = replaced.kind == MaterialAssetKind::Function ? replaced.function.graph : replaced.graph;
    std::string prefix = std::string(callId) + ".";
    while (std::any_of(graph.parameters.begin(), graph.parameters.end(),
                       [&](const auto& p) { return p.id.starts_with(prefix); }))
        prefix += "x";
    std::map<std::pair<std::string, std::string>, std::string> inputs;
    std::vector<MaterialGraphEdgeData> callInputs;
    for (const auto& edge : graph.edges) {
        if (selected.contains(edge.sourceNodeId) || !selected.contains(edge.targetNodeId))
            continue;
        const auto key = std::pair{edge.sourceNodeId, edge.sourcePin};
        auto found = inputs.find(key);
        if (found == inputs.end()) {
            const auto* inputNode = FindMaterialGraphNode(graph, edge.sourceNodeId);
            const auto type =
                inputNode ? ResolveMaterialGraphPinType(graph, *inputNode, edge.sourcePin, true) : std::nullopt;
            if (!type) {
                error = "material.function.connected";
                return false;
            }
            const std::string id = prefix + "input" + std::to_string(inputs.size());
            found = inputs.emplace(key, id).first;
            MaterialGraphParameterData p;
            p.id = id;
            p.displayName = edge.targetPin;
            p.defaultValue.type = *type;
            function.function.inputs.push_back(p);
            extracted.parameters.push_back(p);
            MaterialGraphNodeData n;
            n.id = id;
            n.kind = MaterialNodeKind::Parameter;
            n.referenceId = id;
            n.displayName = edge.targetPin;
            n.valueType = *type;
            n.value.type = *type;
            extracted.nodes.push_back(n);
            callInputs.push_back({edge.id, edge.sourceNodeId, edge.sourcePin, std::string(callId), id});
        }
        extracted.edges.push_back({edge.id, found->second, "Value", edge.targetNodeId, edge.targetPin});
    }
    std::erase_if(target.nodes, [&](const auto& node) { return selected.contains(node.id); });
    std::erase_if(target.edges, [&](const auto& edge) { return selected.contains(edge.targetNodeId); });
    for (auto& edge : target.edges)
        if (selected.contains(edge.sourceNodeId))
            edge.sourceNodeId = callId;
    target.edges.insert(target.edges.end(), callInputs.begin(), callInputs.end());
    MaterialGraphNodeData call;
    call.id = callId;
    call.kind = MaterialNodeKind::FunctionCall;
    call.displayName = name;
    call.position = outputNode->position;
    call.assetReference = functionGuid;
    call.functionInputs = function.function.inputs;
    call.functionOutputs = function.function.outputs;
    call.valueType = call.functionOutputs.front().defaultValue.type;
    call.value.type = call.valueType;
    target.nodes.push_back(call);
    if (selected.contains(target.outputNodeId))
        target.outputNodeId = callId;
    for (auto& group : target.groups) {
        const bool contained = std::any_of(group.nodeIds.begin(), group.nodeIds.end(),
                                           [&](const auto& id) { return selected.contains(id); });
        std::erase_if(group.nodeIds, [&](const auto& id) { return selected.contains(id); });
        if (contained)
            group.nodeIds.emplace_back(callId);
    }
    error.clear();
    return true;
}
} // namespace HIKARI::ASSETS::MATERIAL
