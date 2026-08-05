#include "Assets/Material/Compiler/Internal/HIKARI_MaterialFunctionExpansion.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"

namespace HIKARI::MATERIAL_COMPILER_INTERNAL {
namespace {

struct SourceEndpoint {
    std::string nodeId{};
    std::string pin{};
};

void AddError(
    MaterialCompileResult& result,
    std::string code,
    std::string message,
    std::string subjectId = {}) {
    result.diagnostics.push_back(MaterialCompileDiagnostic{
        MaterialCompileDiagnosticSeverity::Error,
        std::move(code), std::move(message), std::move(subjectId)});
}

MaterialGraphData* SelectGraph(MaterialAssetData& source) noexcept {
    if (source.kind == MaterialAssetKind::Material) {
        return &source.graph;
    }
    if (source.kind == MaterialAssetKind::Function) {
        return &source.function.graph;
    }
    return nullptr;
}

const MaterialGraphNodeData* FindNode(
    const MaterialGraphData& graph,
    std::string_view id) noexcept {
    const auto found = std::find_if(
        graph.nodes.begin(), graph.nodes.end(),
        [id](const MaterialGraphNodeData& node) { return node.id == id; });
    return found == graph.nodes.end() ? nullptr : &*found;
}

std::unordered_set<std::string> CollectReachable(
    const MaterialGraphData& graph) {
    std::unordered_set<std::string> reachable{};
    std::vector<std::string> pending{};
    if (!graph.outputNodeId.empty()) {
        pending.push_back(graph.outputNodeId);
    }
    while (!pending.empty()) {
        std::string id = std::move(pending.back());
        pending.pop_back();
        if (!reachable.emplace(id).second) {
            continue;
        }
        for (const MaterialGraphEdgeData& edge : graph.edges) {
            if (edge.targetNodeId == id) {
                pending.push_back(edge.sourceNodeId);
            }
        }
    }
    return reachable;
}

bool SameInterface(
    const std::vector<MaterialGraphParameterData>& cached,
    const std::vector<MaterialGraphParameterData>& current) {
    if (cached.empty()) {
        return true;
    }
    if (cached.size() != current.size()) {
        return false;
    }
    for (const MaterialGraphParameterData& cachedPin : cached) {
        const auto currentPin = std::find_if(
            current.begin(), current.end(),
            [&cachedPin](const MaterialGraphParameterData& value) {
                return value.id == cachedPin.id;
            });
        if (currentPin == current.end() ||
            cachedPin.defaultValue.type !=
                currentPin->defaultValue.type) {
            return false;
        }
    }
    return true;
}

bool ValidateFunctionInterface(
    const MaterialAssetData& function,
    MaterialCompileResult& result,
    std::string_view subjectId) {
    if (function.kind != MaterialAssetKind::Function) {
        AddError(result, "Material.FunctionKindMismatch",
            "referenced asset is not a Material Function",
            std::string(subjectId));
        return false;
    }
    if (function.function.outputs.empty()) {
        AddError(result, "Material.FunctionOutputArity",
            "Material Function requires at least one declared output",
            std::string(subjectId));
        return false;
    }
    std::unordered_set<std::string> ids{};
    for (const MaterialGraphParameterData& input : function.function.inputs) {
        if (!IsValidMaterialStableId(input.id) ||
            !ids.emplace(input.id).second) {
            AddError(result, "Material.InvalidFunctionInterface",
                "Material Function inputs require stable unique IDs",
                std::string(subjectId));
            return false;
        }
    }
    std::unordered_set<std::string> outputIds{};
    for (const MaterialGraphParameterData& output :
         function.function.outputs) {
        if (!IsValidMaterialStableId(output.id) ||
            !outputIds.emplace(output.id).second) {
            AddError(result, "Material.InvalidFunctionInterface",
                "Material Function outputs require stable unique IDs",
                std::string(subjectId));
            return false;
        }
    }
    return true;
}

std::optional<MaterialGraphEdgeData> FindCallInput(
    const MaterialGraphData& graph,
    const MaterialGraphNodeData& call,
    const MaterialGraphParameterData& input,
    size_t inputCount) {
    const auto found = std::find_if(
        graph.edges.begin(), graph.edges.end(),
        [&call, &input, inputCount](const MaterialGraphEdgeData& edge) {
            return edge.targetNodeId == call.id &&
                (edge.targetPin == input.id ||
                 (inputCount == 1u && edge.targetPin == "Input"));
        });
    return found == graph.edges.end()
        ? std::optional<MaterialGraphEdgeData>{}
        : *found;
}

bool ExpandGraph(
    MaterialGraphData& graph,
    const MaterialCompileOptions& options,
    std::vector<std::string>& callStack,
    std::vector<AssetGuid>& dependencies,
    MaterialCompileResult& result);

bool ExpandCall(
    MaterialGraphData& graph,
    const MaterialGraphNodeData& call,
    const MaterialCompileOptions& options,
    std::vector<std::string>& callStack,
    std::vector<AssetGuid>& dependencies,
    MaterialCompileResult& result) {
    if (!call.assetReference.IsValid()) {
        AddError(result, "Material.MissingFunction",
            "function call requires a Material Function asset GUID", call.id);
        return false;
    }
    if (!options.resolveAsset) {
        AddError(result, "Material.FunctionResolverMissing",
            "Material Function compilation requires an asset resolver",
            call.id);
        return false;
    }
    if (callStack.size() >= options.maximumFunctionDepth) {
        AddError(result, "Material.FunctionDepthExceeded",
            "Material Function nesting exceeds the bounded compiler depth",
            call.id);
        return false;
    }
    if (std::find(callStack.begin(), callStack.end(),
            call.assetReference.value) != callStack.end()) {
        AddError(result, "Material.FunctionCycle",
            "Material Function dependency chain contains a cycle",
            call.assetReference.value);
        return false;
    }

    MaterialAssetData function{};
    std::string loadError{};
    if (!options.resolveAsset(
            call.assetReference, function, loadError)) {
        AddError(result, "Material.FunctionLoadFailed",
            loadError.empty()
                ? "Material Function source could not be loaded"
                : std::move(loadError),
            call.assetReference.value);
        return false;
    }
    if (!ValidateFunctionInterface(function, result, call.id)) {
        return false;
    }
    if (!SameInterface(call.functionInputs, function.function.inputs) ||
        !SameInterface(call.functionOutputs, function.function.outputs)) {
        AddError(result, "Material.FunctionSignatureChanged",
            "cached Function pins no longer match the referenced asset; replace the node to accept the new interface",
            call.id);
        return false;
    }

    callStack.push_back(call.assetReference.value);
    const bool nestedExpanded = ExpandGraph(
        function.function.graph, options, callStack, dependencies, result);
    callStack.pop_back();
    if (!nestedExpanded) {
        return false;
    }

    std::vector<MaterialGraphDiagnostic> graphDiagnostics{};
    if (!ValidateMaterialGraph(
            function.function.graph,
            MaterialGraphValidationOptions{ false },
            graphDiagnostics)) {
        for (const MaterialGraphDiagnostic& diagnostic : graphDiagnostics) {
            if (diagnostic.severity == MaterialGraphDiagnosticSeverity::Error) {
                AddError(result, "Material.FunctionGraphInvalid",
                    diagnostic.message,
                    call.assetReference.value + ":" + diagnostic.subjectId);
            }
        }
        return false;
    }

    const MaterialGraphNodeData* functionOutputNode = FindNode(
        function.function.graph, function.function.graph.outputNodeId);
    if (functionOutputNode == nullptr) {
        AddError(result, "Material.FunctionOutputMissing",
            "Material Function outputNodeId does not resolve to a node",
            call.id);
        return false;
    }
    std::vector<std::string_view> outputPins =
        GetMaterialGraphNodeOutputPins(*functionOutputNode);
    std::unordered_map<std::string, std::string> outputPinById{};
    for (const MaterialGraphParameterData& functionOutput :
         function.function.outputs) {
        const auto namedOutput = std::find(
            outputPins.begin(), outputPins.end(), functionOutput.id);
        if (namedOutput != outputPins.end()) {
            outputPinById.emplace(functionOutput.id, functionOutput.id);
        } else if (function.function.outputs.size() == 1u &&
                   outputPins.size() == 1u) {
            outputPinById.emplace(
                functionOutput.id, std::string(outputPins.front()));
        } else {
            AddError(result, "Material.FunctionOutputPinAmbiguous",
                "each Material Function output ID must name an output pin when the function exposes multiple outputs",
                call.id);
            return false;
        }
    }

    std::unordered_map<std::string, const MaterialGraphParameterData*>
        interfaceInputs{};
    for (const MaterialGraphParameterData& input : function.function.inputs) {
        interfaceInputs.emplace(input.id, &input);
    }
    for (const MaterialGraphEdgeData& edge : graph.edges) {
        if (edge.targetNodeId != call.id) {
            continue;
        }
        const bool declared = interfaceInputs.contains(edge.targetPin) ||
            (interfaceInputs.size() == 1u && edge.targetPin == "Input");
        if (!declared) {
            AddError(result, "Material.UnknownFunctionInput",
                "Function call edge references an undeclared input pin",
                edge.id);
            return false;
        }
    }
    std::unordered_map<std::string, std::string> parameterIds{};
    for (const MaterialGraphParameterData& parameter :
         function.function.graph.parameters) {
        if (interfaceInputs.contains(parameter.id)) {
            continue;
        }
        MaterialGraphParameterData clone = parameter;
        clone.id = call.id + "." + parameter.id;
        parameterIds.emplace(parameter.id, clone.id);
        graph.parameters.push_back(std::move(clone));
    }
    std::unordered_map<std::string, std::string> resourceIds{};
    for (const MaterialGraphResourceData& resource :
         function.function.graph.resources) {
        MaterialGraphResourceData clone = resource;
        clone.id = call.id + "." + resource.id;
        resourceIds.emplace(resource.id, clone.id);
        graph.resources.push_back(std::move(clone));
    }

    std::unordered_map<std::string, SourceEndpoint> endpoints{};
    for (const MaterialGraphNodeData& node : function.function.graph.nodes) {
        if (node.kind == MaterialNodeKind::Parameter) {
            const auto interface = interfaceInputs.find(node.referenceId);
            if (interface != interfaceInputs.end()) {
                const auto incoming = FindCallInput(
                    graph, call, *interface->second, interfaceInputs.size());
                if (incoming.has_value()) {
                    endpoints.emplace(node.id, SourceEndpoint{
                        incoming->sourceNodeId, incoming->sourcePin});
                    continue;
                }
                if (interface->second->defaultValue.type !=
                        MaterialValueType::Bool &&
                    !IsMaterialNumericType(
                        interface->second->defaultValue.type)) {
                    AddError(result, "Material.FunctionInputRequired",
                        "unconnected Function inputs require a literal default",
                        call.id + ":" + interface->second->id);
                    return false;
                }
                MaterialGraphNodeData clone = node;
                clone.id = call.id + "." + node.id;
                clone.kind = MaterialNodeKind::Constant;
                clone.referenceId.clear();
                clone.valueType = interface->second->defaultValue.type;
                clone.value = interface->second->defaultValue;
                clone.displayName = interface->second->displayName;
                endpoints.emplace(node.id, SourceEndpoint{
                    clone.id, "Value"});
                graph.nodes.push_back(std::move(clone));
                continue;
            }
        }

        MaterialGraphNodeData clone = node;
        clone.id = call.id + "." + node.id;
        if (clone.kind == MaterialNodeKind::Parameter) {
            const auto mapped = parameterIds.find(clone.referenceId);
            if (mapped != parameterIds.end()) {
                clone.referenceId = mapped->second;
            }
        } else if (clone.kind == MaterialNodeKind::TextureSample) {
            const auto mapped = resourceIds.find(clone.referenceId);
            if (mapped != resourceIds.end()) {
                clone.referenceId = mapped->second;
            }
        }
        endpoints.emplace(node.id, SourceEndpoint{ clone.id, {} });
        graph.nodes.push_back(std::move(clone));
    }

    for (const MaterialGraphEdgeData& edge : function.function.graph.edges) {
        const auto source = endpoints.find(edge.sourceNodeId);
        const auto target = endpoints.find(edge.targetNodeId);
        if (source == endpoints.end() || target == endpoints.end() ||
            target->second.nodeId.empty()) {
            AddError(result, "Material.FunctionExpansionFailed",
                "Material Function graph could not be mapped into the caller",
                call.id);
            return false;
        }
        MaterialGraphEdgeData clone = edge;
        clone.id = call.id + "." + edge.id;
        clone.sourceNodeId = source->second.nodeId;
        clone.sourcePin = source->second.pin.empty()
            ? edge.sourcePin
            : source->second.pin;
        clone.targetNodeId = target->second.nodeId;
        graph.edges.push_back(std::move(clone));
    }

    const auto outputEndpoint = endpoints.find(functionOutputNode->id);
    if (outputEndpoint == endpoints.end()) {
        AddError(result, "Material.FunctionExpansionFailed",
            "Material Function output could not be mapped into the caller",
            call.id);
        return false;
    }
    for (MaterialGraphEdgeData& edge : graph.edges) {
        if (edge.sourceNodeId != call.id) {
            continue;
        }
        std::string outputId = edge.sourcePin;
        if (function.function.outputs.size() == 1u &&
            outputId == "Result") {
            outputId = function.function.outputs.front().id;
        }
        const auto outputPin = outputPinById.find(outputId);
        if (outputPin == outputPinById.end()) {
            AddError(result, "Material.UnknownFunctionOutput",
                "Function call edge references an undeclared output pin",
                edge.id);
            return false;
        }
        edge.sourceNodeId = outputEndpoint->second.nodeId;
        edge.sourcePin = outputEndpoint->second.pin.empty()
            ? outputPin->second
            : outputEndpoint->second.pin;
    }
    if (graph.outputNodeId == call.id) {
        graph.outputNodeId = outputEndpoint->second.nodeId;
    }

    std::erase_if(graph.edges, [&call](const MaterialGraphEdgeData& edge) {
        return edge.targetNodeId == call.id;
    });
    std::erase_if(graph.nodes, [&call](const MaterialGraphNodeData& node) {
        return node.id == call.id;
    });
    dependencies.push_back(call.assetReference);
    return true;
}

bool ExpandGraph(
    MaterialGraphData& graph,
    const MaterialCompileOptions& options,
    std::vector<std::string>& callStack,
    std::vector<AssetGuid>& dependencies,
    MaterialCompileResult& result) {
    for (;;) {
        const std::unordered_set<std::string> reachable =
            CollectReachable(graph);
        const auto call = std::find_if(
            graph.nodes.begin(), graph.nodes.end(),
            [&reachable](const MaterialGraphNodeData& node) {
                return node.kind == MaterialNodeKind::FunctionCall &&
                    reachable.contains(node.id);
            });
        if (call == graph.nodes.end()) {
            return true;
        }
        const MaterialGraphNodeData callCopy = *call;
        if (!ExpandCall(
                graph, callCopy, options, callStack,
                dependencies, result)) {
            return false;
        }
    }
}

} // namespace

bool ExpandMaterialFunctions(
    const MaterialAssetData& source,
    const MaterialCompileOptions& options,
    MaterialAssetData& outExpanded,
    std::vector<AssetGuid>& outFunctionDependencies,
    MaterialCompileResult& result) {
    outExpanded = source;
    outFunctionDependencies.clear();
    MaterialGraphData* graph = SelectGraph(outExpanded);
    if (graph == nullptr) {
        return true;
    }
    if (options.maximumFunctionDepth == 0u) {
        AddError(result, "Material.FunctionDepthExceeded",
            "maximum Function depth must be positive");
        return false;
    }
    std::vector<std::string> callStack{};
    const bool expanded = ExpandGraph(
        *graph, options, callStack, outFunctionDependencies, result);
    std::sort(
        outFunctionDependencies.begin(), outFunctionDependencies.end(),
        [](const AssetGuid& left, const AssetGuid& right) {
            return left.value < right.value;
        });
    outFunctionDependencies.erase(
        std::unique(
            outFunctionDependencies.begin(),
            outFunctionDependencies.end()),
        outFunctionDependencies.end());
    return expanded;
}

} // namespace HIKARI::MATERIAL_COMPILER_INTERNAL
