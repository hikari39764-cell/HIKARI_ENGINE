#include "Assets/Material/Compiler/Internal/HIKARI_MaterialProgramLowering.h"

#include <algorithm>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace HIKARI::MATERIAL_COMPILER_INTERNAL {
namespace {

MaterialProgramOpcode ToProgramOpcode(MaterialNodeKind kind) noexcept {
  switch (kind) {
  case MaterialNodeKind::Constant: return MaterialProgramOpcode::Constant;
  case MaterialNodeKind::Parameter: return MaterialProgramOpcode::Parameter;
  case MaterialNodeKind::TextureSample: return MaterialProgramOpcode::TextureSample;
  case MaterialNodeKind::Add: return MaterialProgramOpcode::Add;
  case MaterialNodeKind::Multiply: return MaterialProgramOpcode::Multiply;
  case MaterialNodeKind::Lerp: return MaterialProgramOpcode::Lerp;
  case MaterialNodeKind::Clamp: return MaterialProgramOpcode::Clamp;
  case MaterialNodeKind::Saturate: return MaterialProgramOpcode::Saturate;
  case MaterialNodeKind::OneMinus: return MaterialProgramOpcode::OneMinus;
  case MaterialNodeKind::Power: return MaterialProgramOpcode::Power;
  case MaterialNodeKind::Dot: return MaterialProgramOpcode::Dot;
  case MaterialNodeKind::NormalUnpack: return MaterialProgramOpcode::NormalUnpack;
  case MaterialNodeKind::PbrSurface: return MaterialProgramOpcode::PbrSurface;
  case MaterialNodeKind::RampDiffuse: return MaterialProgramOpcode::RampDiffuse;
  case MaterialNodeKind::MultiRampDiffuse: return MaterialProgramOpcode::MultiRampDiffuse;
  case MaterialNodeKind::SpecularBand: return MaterialProgramOpcode::SpecularBand;
  case MaterialNodeKind::FresnelRim: return MaterialProgramOpcode::FresnelRim;
  case MaterialNodeKind::MatCap: return MaterialProgramOpcode::MatCap;
  case MaterialNodeKind::AnisotropicHair: return MaterialProgramOpcode::AnisotropicHair;
  case MaterialNodeKind::FaceSdf: return MaterialProgramOpcode::FaceSdf;
  case MaterialNodeKind::EyeLighting: return MaterialProgramOpcode::EyeLighting;
  case MaterialNodeKind::SurfaceOutput: return MaterialProgramOpcode::SurfaceOutput;
  case MaterialNodeKind::FunctionCall: return MaterialProgramOpcode::FunctionCall;
  case MaterialNodeKind::SkinLighting: return MaterialProgramOpcode::SkinLighting;
  case MaterialNodeKind::FabricSheen: return MaterialProgramOpcode::FabricSheen;
  case MaterialNodeKind::HybridLighting: return MaterialProgramOpcode::HybridLighting;
  case MaterialNodeKind::Outline: return MaterialProgramOpcode::Outline;
  }
  return MaterialProgramOpcode::Constant;
}

std::vector<std::string_view> InputPins(MaterialNodeKind kind) {
  switch (kind) {
  case MaterialNodeKind::Add:
  case MaterialNodeKind::Multiply:
  case MaterialNodeKind::Dot: return {"A", "B"};
  case MaterialNodeKind::Lerp: return {"A", "B", "Alpha"};
  case MaterialNodeKind::Clamp: return {"Value", "Min", "Max"};
  case MaterialNodeKind::Saturate:
  case MaterialNodeKind::OneMinus: return {"Value"};
  case MaterialNodeKind::Power: return {"Base", "Exponent"};
  case MaterialNodeKind::NormalUnpack: return {"PackedNormal"};
  case MaterialNodeKind::PbrSurface:
    return {"BaseColor", "Normal", "Metallic", "Roughness", "Occlusion",
            "Emissive", "Specular", "SpecularColor", "DiffuseResponse",
            "SpecularResponse", "Additive", "Outline"};
  case MaterialNodeKind::RampDiffuse:
    return {"ShadowThreshold", "ShadowSoftness", "MidThreshold",
            "ShadowTint", "MidTint", "LitTint", "MaskInfluence",
            "Intensity", "DirectGain", "Mask"};
  case MaterialNodeKind::MultiRampDiffuse:
    return {"ShadowThreshold", "ShadowSoftness", "MidThreshold",
            "ShadowTint", "MidTint", "LitTint", "RampRow",
            "MaskInfluence", "RampBlend", "Intensity", "DirectGain",
            "Mask"};
  case MaterialNodeKind::SpecularBand:
    return {"Width", "Softness", "Intensity", "Power", "Tint"};
  case MaterialNodeKind::FresnelRim:
    return {"Power", "Width", "Color", "Intensity", "LightAlignment"};
  case MaterialNodeKind::MatCap: return {"Normal", "ViewDirection", "Mask"};
  case MaterialNodeKind::AnisotropicHair: return {"Tangent", "Shift", "Width", "Mask"};
  case MaterialNodeKind::FaceSdf:
    return {"LightDirection", "SDF", "Softness", "DirectGain"};
  case MaterialNodeKind::EyeLighting: return {"BaseColor", "Mask", "ViewDirection"};
  case MaterialNodeKind::SkinLighting:
    return {"DiffuseResponse", "Wrap", "Subsurface", "Tint", "Mask"};
  case MaterialNodeKind::FabricSheen:
    return {"Sheen", "FiberRoughness", "Power", "Intensity", "Mask"};
  case MaterialNodeKind::HybridLighting:
    return {"DiffuseResponse", "DirectDiffuseBlend",
            "DirectSpecularBlend", "IndirectBlend"};
  case MaterialNodeKind::Outline:
    return {"Color", "Thickness"};
  case MaterialNodeKind::SurfaceOutput: return {"Surface"};
  case MaterialNodeKind::FunctionCall: return {"Input"};
  default: return {};
  }
}

std::vector<std::string_view> OutputPins(MaterialNodeKind kind) {
  switch (kind) {
  case MaterialNodeKind::Constant:
  case MaterialNodeKind::Parameter: return {"Value"};
  case MaterialNodeKind::TextureSample:
    return {"Color", "RGB", "R", "G", "B", "A", "Alpha"};
  case MaterialNodeKind::PbrSurface: return {"Surface"};
  case MaterialNodeKind::RampDiffuse:
  case MaterialNodeKind::MultiRampDiffuse:
  case MaterialNodeKind::SpecularBand:
  case MaterialNodeKind::AnisotropicHair:
  case MaterialNodeKind::FaceSdf: return {"Response"};
  case MaterialNodeKind::SkinLighting:
  case MaterialNodeKind::HybridLighting: return {"Response"};
  case MaterialNodeKind::FresnelRim:
  case MaterialNodeKind::MatCap:
  case MaterialNodeKind::EyeLighting: return {"Color"};
  case MaterialNodeKind::FabricSheen: return {"Color"};
  case MaterialNodeKind::Outline: return {"Response"};
  case MaterialNodeKind::SurfaceOutput: return {};
  case MaterialNodeKind::NormalUnpack: return {"Normal"};
  default: return {"Result"};
  }
}

std::string PinKey(std::string_view nodeId, std::string_view pin) {
  std::string key{};
  key.reserve(nodeId.size() + pin.size() + 1u);
  key.append(nodeId);
  key.push_back(':');
  key.append(pin);
  return key;
}

std::unordered_set<std::string> CollectReachable(
    const MaterialGraphData& graph) {
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

void BuildMaterialProgramInstructions(const MaterialGraphData& graph,
                                      MaterialProgramIR& program) {
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
    if (reachable.contains(edge.sourceNodeId) &&
        reachable.contains(edge.targetNodeId)) {
      ++indegree[edge.targetNodeId];
      successors[edge.sourceNodeId].push_back(edge.targetNodeId);
    }
  }

  std::set<std::string> ready{};
  for (const auto& [nodeId, degree] : indegree) {
    if (degree == 0u) ready.emplace(nodeId);
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
    instruction.opcode = ToProgramOpcode(node.kind);
    instruction.valueType = node.valueType;
    instruction.immediate = node.value;
    instruction.properties = node.properties;
    std::sort(instruction.properties.begin(), instruction.properties.end(),
              [](const auto& left, const auto& right) {
                return left.id < right.id;
              });
    instruction.assetReference = node.assetReference;
    instruction.sourceNodeId = node.id;
    if (node.kind == MaterialNodeKind::Parameter) {
      const auto binding = parameterBindings.find(node.referenceId);
      if (binding != parameterBindings.end()) {
        instruction.bindingIndex = binding->second;
        instruction.valueType = program.parameters[binding->second].type;
      }
    } else if (node.kind == MaterialNodeKind::Constant) {
      instruction.valueType = node.value.type;
    } else if (node.kind == MaterialNodeKind::TextureSample) {
      const auto binding = resourceBindings.find(node.referenceId);
      if (binding != resourceBindings.end()) instruction.bindingIndex = binding->second;
    }

    for (const std::string_view inputPin : InputPins(node.kind)) {
      instruction.inputPins.emplace_back(inputPin);
      uint32_t sourceRegister = kInvalidMaterialProgramRegister;
      const auto edge = std::find_if(
          graph.edges.begin(), graph.edges.end(),
          [&node, inputPin](const MaterialGraphEdgeData& candidate) {
            return candidate.targetNodeId == node.id &&
                   candidate.targetPin == inputPin;
          });
      if (edge != graph.edges.end()) {
        const auto source = outputRegisters.find(
            PinKey(edge->sourceNodeId, edge->sourcePin));
        if (source != outputRegisters.end()) sourceRegister = source->second;
      }
      instruction.inputRegisters.push_back(sourceRegister);
    }
    for (const std::string_view outputPin : OutputPins(node.kind)) {
      instruction.outputPins.emplace_back(outputPin);
      instruction.outputRegisters.push_back(nextRegister);
      outputRegisters.emplace(PinKey(node.id, outputPin), nextRegister++);
    }
    if (node.kind == MaterialNodeKind::SurfaceOutput &&
        !instruction.inputRegisters.empty()) {
      program.surfaceOutputRegister = instruction.inputRegisters.front();
    }
    program.instructions.push_back(std::move(instruction));

    auto next = successors.find(nodeId);
    if (next == successors.end()) continue;
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
