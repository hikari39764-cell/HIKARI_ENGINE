#include "Assets/Material/Compiler/Internal/HIKARI_MaterialProgramLowering.h"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"

namespace HIKARI::MATERIAL_COMPILER_INTERNAL {
namespace {

MaterialProgramOpcode ToProgramOpcode(const MaterialNodeTypeId& typeId) noexcept {
    MaterialNodeKind kind = MaterialNodeKind::Constant;
    (void)TryGetLegacyMaterialNodeKind(typeId, kind);
    return kind;
}

std::string PinKey(std::string_view nodeId, std::string_view pin) {
    std::string key{};
    key.reserve(nodeId.size() + pin.size() + 1u);
    key.append(nodeId);
    key.push_back(':');
    key.append(pin);
    return key;
}

std::unordered_set<std::string> CollectReachable(const MaterialGraphData& graph) {
    std::unordered_set<std::string> reachable{};
    if (graph.outputNodeId.empty()) {
        return reachable;
    }
    std::vector<std::string> pending{graph.outputNodeId};
    while (!pending.empty()) {
        std::string nodeId = std::move(pending.back());
        pending.pop_back();
        if (!reachable.emplace(nodeId).second) {
            continue;
        }
        for (const MaterialGraphEdgeData& edge : graph.edges) {
            if (edge.targetNodeId == nodeId) {
                pending.push_back(edge.sourceNodeId);
            }
        }
    }
    return reachable;
}

} // namespace

void BuildMaterialProgramInstructions(const MaterialGraphData& graph, MaterialProgramIR& program) {
    const std::unordered_set<std::string> reachable = CollectReachable(graph);
    std::unordered_map<std::string, const MaterialGraphNodeData*> nodes{};
    std::unordered_map<std::string, uint32_t> indegree{};
    std::unordered_map<std::string, std::vector<std::string>> successors{};
    for (const MaterialGraphNodeData& node : graph.nodes) {
        if (reachable.contains(node.id)) {
            nodes.emplace(node.id, &node);
            indegree.emplace(node.id, 0u);
        }
    }
    for (const MaterialGraphEdgeData& edge : graph.edges) {
        if (reachable.contains(edge.sourceNodeId) && reachable.contains(edge.targetNodeId)) {
            ++indegree[edge.targetNodeId];
            successors[edge.sourceNodeId].push_back(edge.targetNodeId);
        }
    }

    std::set<std::string> ready{};
    for (const auto& [nodeId, degree] : indegree) {
        if (degree == 0u)
            ready.emplace(nodeId);
    }
    std::unordered_map<std::string, uint32_t> parameterBindings{};
    for (uint32_t index = 0u; index < program.parameters.size(); ++index) {
        parameterBindings.emplace(program.parameters[index].parameterId, index);
    }
    std::unordered_map<std::string, uint32_t> resourceBindings{};
    for (uint32_t index = 0u; index < program.resources.size(); ++index) {
        resourceBindings.emplace(program.resources[index].resourceId, index);
    }

    std::unordered_map<std::string, uint32_t> outputRegisters{};
    uint32_t nextRegister = 0u;
    while (!ready.empty()) {
        const std::string nodeId = *ready.begin();
        ready.erase(ready.begin());
        const MaterialGraphNodeData& node = *nodes.at(nodeId);
        MaterialProgramInstruction instruction{};
        instruction.opcode =
            node.kind == MaterialNodeKind::Variable ? MaterialProgramOpcode::Parameter : ToProgramOpcode(node.kind);
        instruction.valueType = node.valueType;
        instruction.immediate = node.value;
        instruction.properties = node.properties;
        std::sort(instruction.properties.begin(), instruction.properties.end(),
                  [](const auto& left, const auto& right) { return left.id < right.id; });
        instruction.assetReference = node.assetReference;
        instruction.sourceNodeId = node.id;
        if (node.kind == MaterialNodeKind::Parameter || node.kind == MaterialNodeKind::Variable) {
            const std::string bindingId =
                node.kind == MaterialNodeKind::Variable ? MakeMaterialVariableParameterId(node.id) : node.referenceId;
            const auto binding = parameterBindings.find(bindingId);
            if (binding != parameterBindings.end()) {
                instruction.bindingIndex = binding->second;
                instruction.valueType = program.parameters[binding->second].type;
            }
        } else if (node.kind == MaterialNodeKind::Constant) {
            instruction.valueType = node.value.type;
        } else if (node.kind == MaterialNodeKind::TextureSample) {
            const auto binding = resourceBindings.find(node.referenceId);
            if (binding != resourceBindings.end())
                instruction.bindingIndex = binding->second;
        }

        for (const std::string_view inputPin : GetMaterialGraphNodeInputPins(node)) {
            instruction.inputPins.emplace_back(inputPin);
            uint32_t sourceRegister = kInvalidMaterialProgramRegister;
            const auto edge = std::find_if(
                graph.edges.begin(), graph.edges.end(), [&node, inputPin](const MaterialGraphEdgeData& candidate) {
                    return candidate.targetNodeId == node.id && candidate.targetPin == inputPin;
                });
            if (edge != graph.edges.end()) {
                const auto source = outputRegisters.find(PinKey(edge->sourceNodeId, edge->sourcePin));
                if (source != outputRegisters.end())
                    sourceRegister = source->second;
            }
            instruction.inputRegisters.push_back(sourceRegister);
        }
        for (const std::string_view outputPin : GetMaterialGraphNodeOutputPins(node)) {
            instruction.outputPins.emplace_back(outputPin);
            instruction.outputRegisters.push_back(nextRegister);
            outputRegisters.emplace(PinKey(node.id, outputPin), nextRegister++);
        }
        if (node.kind == MaterialNodeKind::SurfaceOutput && !instruction.inputRegisters.empty()) {
            program.surfaceOutputRegister = instruction.inputRegisters.front();
        }
        program.instructions.push_back(std::move(instruction));

        auto next = successors.find(nodeId);
        if (next == successors.end())
            continue;
        std::sort(next->second.begin(), next->second.end());
        for (const std::string& target : next->second) {
            auto degree = indegree.find(target);
            if (degree != indegree.end() && --degree->second == 0u) {
                ready.emplace(target);
            }
        }
    }
    program.registerCount = nextRegister;
}

} // namespace HIKARI::MATERIAL_COMPILER_INTERNAL
