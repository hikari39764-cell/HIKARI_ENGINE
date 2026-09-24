#include "Assets/Material/Authoring/HIKARI_MaterialGraphInspection.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphQuery.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Assets/Material/Graph/HIKARI_MaterialNodeDefinition.h"
#include <algorithm>

namespace HIKARI::ASSETS::MATERIAL {
bool BuildMaterialNodePreview(const MaterialAssetData& source, std::string_view nodeId, std::string_view pin,
                              int channel, MaterialAssetData& result, std::string& error) {
    if (source.domain != MaterialDomain::Surface ||
        (source.kind != MaterialAssetKind::Material && source.kind != MaterialAssetKind::Function)) {
        error = "material.inspect.domain";
        return false;
    }
    const auto& graph = source.kind == MaterialAssetKind::Function ? source.function.graph : source.graph;
    const auto* node = FindMaterialGraphNode(graph, nodeId);
    const auto type = node ? ResolveMaterialGraphPinType(graph, *node, pin, true) : std::nullopt;
    if (!type || !IsMaterialNumericType(*type) || channel < -1 || channel > 3) {
        error = "material.inspect.numeric";
        return false;
    }
    const auto* definition = FindMaterialNodeDefinition(node->kind);
    if (definition && definition->outputRoute != MaterialNodeOutputRoute::Unrestricted) {
        error = "material.inspect.response";
        return false;
    }
    result = source;
    result.kind = MaterialAssetKind::Material;
    result.templateId = MaterialTemplateId::Custom;
    result.graph = graph;
    result.function = {};
    result.surface.unlit = true;
    result.surface.alphaMode = MaterialAlphaMode::Opaque;
    result.pbr.unlit = true;
    auto& preview = result.graph;
    const auto reachable = CollectMaterialGraphUpstreamNodes(graph, nodeId);
    std::erase_if(preview.nodes, [&](const auto& item) { return !reachable.contains(item.id); });
    std::erase_if(preview.edges, [&](const auto& edge) {
        return !reachable.contains(edge.targetNodeId) || !reachable.contains(edge.sourceNodeId);
    });
    preview.groups.clear();
    std::string prefix = "inspect.";
    while (std::any_of(preview.nodes.begin(), preview.nodes.end(),
                       [&](const auto& item) { return item.id.starts_with(prefix); }))
        prefix += "x";
    const auto add = [&](std::string_view suffix, MaterialNodeKind kind, MaterialValueType valueType) {
        MaterialGraphNodeData item;
        item.id = prefix + std::string(suffix);
        item.kind = kind;
        item.valueType = valueType;
        item.value.type = valueType;
        preview.nodes.push_back(item);
        return item.id;
    };
    const auto link = [&](const std::string& from, std::string_view fromPin, const std::string& to,
                          std::string_view toPin) {
        preview.edges.push_back({prefix + "edge" + std::to_string(preview.edges.size()), from, std::string(fromPin), to,
                                 std::string(toPin)});
    };
    std::string valueNode(nodeId), valuePin(pin);
    if (*type != MaterialValueType::Float1) {
        const auto split = add("split", MaterialNodeKind::Split, *type);
        link(valueNode, valuePin, split, "Value");
        if (channel >= 0) {
            valueNode = split;
            valuePin = std::string(1, "XYZW"[channel]);
        } else {
            const auto color = add("rgb", MaterialNodeKind::MakeFloat3, MaterialValueType::Float3);
            link(split, "X", color, "X");
            link(split, "Y", color, "Y");
            if (*type != MaterialValueType::Float2)
                link(split, "Z", color, "Z");
            valueNode = color;
            valuePin = "Value";
        }
    }
    const auto black = add("black", MaterialNodeKind::Constant, MaterialValueType::Float1);
    const auto surface = add("surface", MaterialNodeKind::PbrSurface, MaterialValueType::Surface);
    const auto output = add("output", MaterialNodeKind::SurfaceOutput, MaterialValueType::Surface);
    link(valueNode, valuePin, surface, "Emissive");
    link(black, "Value", surface, "BaseColor");
    link(surface, "Surface", output, "Surface");
    preview.outputNodeId = output;
    ReconcileMaterialGraphDynamicNodeTypes(preview);
    error.clear();
    return true;
}
} // namespace HIKARI::ASSETS::MATERIAL
