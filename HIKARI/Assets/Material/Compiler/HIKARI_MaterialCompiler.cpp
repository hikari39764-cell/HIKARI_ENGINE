#include "Assets/Material/Graph/HIKARI_MaterialGraphQuery.h"
#include "HIKARI_MaterialCompiler.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <type_traits>

#include "Assets/Material/Compiler/Internal/HIKARI_MaterialCompilerValidation.h"
#include "Assets/Material/Compiler/Internal/HIKARI_MaterialFunctionExpansion.h"
#include "Assets/Material/Compiler/Internal/HIKARI_MaterialProgramLowering.h"
#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Assets/Material/HIKARI_MaterialDependencies.h"
#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"
#include "Assets/Material/Serialization/HIKARI_MaterialShellFields.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Core/Text/HIKARI_HexString.h"

namespace HIKARI {

namespace {

void AddDiagnostic(MaterialCompileResult& result, MaterialCompileDiagnosticSeverity severity, std::string code,
                   std::string message, std::string subjectId = {}) {
    result.diagnostics.push_back(
        MaterialCompileDiagnostic{severity, std::move(code), std::move(message), std::move(subjectId)});
}

void AddCapability(uint64_t& mask, MaterialProgramCapability capability) noexcept {
    mask |= MaterialCapabilityBit(capability);
}

struct ReachableBindingIds {
    std::unordered_set<std::string> parameters{};
    std::unordered_set<std::string> resources{};
};

ReachableBindingIds CollectReachableBindingIds(const MaterialGraphData& graph,
                                               const std::unordered_set<std::string>& reachable) {
    ReachableBindingIds bindings{};
    for (const MaterialGraphNodeData& node : graph.nodes) {
        if (!reachable.contains(node.id)) {
            continue;
        }
        if (node.kind == MaterialNodeKind::Parameter && !node.referenceId.empty()) {
            bindings.parameters.emplace(node.referenceId);
        }
        if (node.kind == MaterialNodeKind::TextureSample && !node.referenceId.empty()) {
            bindings.resources.emplace(node.referenceId);
        }
        for (const std::string_view id : ASSETS::MATERIAL::GetMaterialNodeOwnedParameterIds(node.kind)) {
            bindings.parameters.emplace(id);
        }
        for (const std::string_view id : ASSETS::MATERIAL::GetMaterialNodeOwnedResourceIds(node.kind)) {
            bindings.resources.emplace(id);
        }
    }
    return bindings;
}

void RetainReachableBindings(const MaterialGraphData& graph, const std::unordered_set<std::string>& reachable,
                             std::vector<MaterialGraphParameterData>& parameters,
                             std::vector<MaterialGraphResourceData>& resources) {
    const ReachableBindingIds bindings = CollectReachableBindingIds(graph, reachable);
    std::erase_if(parameters,
                  [&bindings](const auto& parameter) { return !bindings.parameters.contains(parameter.id); });
    std::erase_if(resources, [&bindings](const auto& resource) { return !bindings.resources.contains(resource.id); });
}

void AddNodeCapabilities(const MaterialGraphData& graph, uint64_t& capabilities) {
    const std::unordered_set<std::string> reachable = CollectMaterialGraphUpstreamNodes(graph);

    for (const MaterialGraphNodeData& node : graph.nodes) {
        if (!reachable.contains(node.id)) {
            continue;
        }
        MaterialNodeKind builtinKind{};
        if (!TryGetBuiltinMaterialNodeKind(node.kind, builtinKind)) {
            continue;
        }
        switch (builtinKind) {
        case MaterialNodeKind::PbrSurface: AddCapability(capabilities, MaterialProgramCapability::StandardPbr); break;
        case MaterialNodeKind::Shell: AddCapability(capabilities, MaterialProgramCapability::Shell); break;
        case MaterialNodeKind::RampDiffuse: AddCapability(capabilities, MaterialProgramCapability::RampDiffuse); break;
        case MaterialNodeKind::MultiRampDiffuse:
            AddCapability(capabilities, MaterialProgramCapability::MultiRampDiffuse);
            break;
        case MaterialNodeKind::SpecularBand:
            AddCapability(capabilities, MaterialProgramCapability::SpecularBand);
            break;
        case MaterialNodeKind::FresnelRim: AddCapability(capabilities, MaterialProgramCapability::FresnelRim); break;
        case MaterialNodeKind::MatCap: AddCapability(capabilities, MaterialProgramCapability::MatCap); break;
        case MaterialNodeKind::AnisotropicHair:
            AddCapability(capabilities, MaterialProgramCapability::AnisotropicHair);
            break;
        case MaterialNodeKind::FaceSdf: AddCapability(capabilities, MaterialProgramCapability::FaceSdf); break;
        case MaterialNodeKind::EyeLighting: AddCapability(capabilities, MaterialProgramCapability::EyeLighting); break;
        case MaterialNodeKind::SkinLighting:
            AddCapability(capabilities, MaterialProgramCapability::SkinLighting);
            break;
        case MaterialNodeKind::FabricSheen: AddCapability(capabilities, MaterialProgramCapability::FabricSheen); break;
        case MaterialNodeKind::HybridLighting:
            AddCapability(capabilities, MaterialProgramCapability::HybridLighting);
            break;
        case MaterialNodeKind::Outline: AddCapability(capabilities, MaterialProgramCapability::Outline); break;
        case MaterialNodeKind::HairShadow: AddCapability(capabilities, MaterialProgramCapability::HairShadow); break;
        case MaterialNodeKind::CorneaHighlight:
            AddCapability(capabilities, MaterialProgramCapability::EyeLighting);
            AddCapability(capabilities, MaterialProgramCapability::CorneaHighlight);
            break;
        case MaterialNodeKind::ParticleOutput:
            AddCapability(capabilities, MaterialProgramCapability::ParticleExpression);
            break;
        case MaterialNodeKind::FunctionCall:
            AddCapability(capabilities, MaterialProgramCapability::FunctionCalls);
            break;
        default: break;
        }
    }
}

void AddReachableOutlineFeatureRequests(const MaterialGraphData& graph, std::vector<MaterialFeatureRequest>& requests) {
    const std::unordered_set<std::string> reachable = CollectMaterialGraphUpstreamNodes(graph);
    const bool hasOutline =
        std::any_of(graph.nodes.begin(), graph.nodes.end(), [&reachable](const MaterialGraphNodeData& node) {
            return node.kind == MaterialNodeKind::Outline && reachable.contains(node.id);
        });
    std::erase_if(requests, [](MaterialFeatureRequest request) {
        return request == MaterialFeatureRequest::GeometryOutline || request == MaterialFeatureRequest::ScreenOutline ||
               // The legacy screen-space FaceHairShadow flag has no
               // authored texture node and therefore cannot be inferred
               // from the connected graph. Do not retain it as hidden
               // template behavior.
               request == MaterialFeatureRequest::FaceHairShadow;
    });
    if (!hasOutline) {
        return;
    }
    float mode = 2.0f;
    const auto parameter =
        std::find_if(graph.parameters.begin(), graph.parameters.end(),
                     [](const MaterialGraphParameterData& value) { return value.id == "parameter.outlineMode"; });
    if (parameter != graph.parameters.end()) {
        mode = parameter->defaultValue.numeric.x;
    }
    if (mode < 0.5f || mode >= 1.5f) {
        requests.push_back(MaterialFeatureRequest::GeometryOutline);
    }
    if (mode >= 0.5f) {
        requests.push_back(MaterialFeatureRequest::ScreenOutline);
    }
}

std::string BuildStructureDescription(const MaterialAssetData& source, const MaterialProgramIR& program) {
    std::ostringstream stream{};
    stream << kMaterialProgramIrVersion << ':' << ToString(source.kind) << ':' << ToString(source.domain) << ':'
           << ToString(program.templateId) << ':' << source.profileAssetGuid.value << ':'
           << ToString(source.surface.alphaMode) << ':' << ToString(source.surface.alphaRepresentation) << ':'
           << source.surface.doubleSided << ':' << source.surface.unlit << ':' << program.capabilities;
    for (const MaterialFeatureRequest feature : program.featureRequests) {
        stream << "|F:" << ToString(feature);
    }
    if (const MaterialGraphData* graph = source.ActiveGraph()) {
        const std::unordered_set<std::string> reachable = CollectMaterialGraphUpstreamNodes(*graph);
        auto nodes = graph->nodes;
        auto edges = graph->edges;
        auto parameters = graph->parameters;
        auto resources = graph->resources;
        std::erase_if(nodes, [&reachable](const auto& node) { return !reachable.contains(node.id); });
        std::erase_if(edges, [&reachable](const auto& edge) {
            return !reachable.contains(edge.sourceNodeId) || !reachable.contains(edge.targetNodeId);
        });
        RetainReachableBindings(*graph, reachable, parameters, resources);
        std::sort(nodes.begin(), nodes.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
        std::sort(edges.begin(), edges.end(), [](const auto& left, const auto& right) { return left.id < right.id; });
        std::sort(parameters.begin(), parameters.end(),
                  [](const auto& left, const auto& right) { return left.id < right.id; });
        std::sort(resources.begin(), resources.end(),
                  [](const auto& left, const auto& right) { return left.id < right.id; });
        for (const auto& node : nodes) {
            stream << "|N:" << node.id << ':' << ToString(node.kind) << ':' << ToString(node.valueType) << ':'
                   << node.referenceId << ':' << node.assetReference.value;
            auto properties = node.properties;
            std::sort(properties.begin(), properties.end(),
                      [](const auto& left, const auto& right) { return left.id < right.id; });
            for (const auto& property : properties) {
                stream << ":Property(" << property.id << ',' << ToString(property.value.type) << ')';
            }
        }
        for (const auto& edge : edges) {
            stream << "|E:" << edge.sourceNodeId << ':' << edge.sourcePin << ':' << edge.targetNodeId << ':'
                   << edge.targetPin;
        }
        for (const auto& parameter : parameters) {
            stream << "|P:" << parameter.id << ':' << ToString(parameter.defaultValue.type);
        }
        for (const auto& resource : resources) {
            stream << "|R:" << resource.id << ':' << ToString(resource.type) << ':' << ToString(resource.semantic);
        }
    }
    return stream.str();
}

MaterialProgramIR BuildProgram(const MaterialAssetData& source,
                               const std::vector<AssetGuid>& functionDependencies = {}) {
    MaterialProgramIR program{};
    program.sourceKind = source.kind;
    program.domain = source.domain;
    program.templateId = source.kind == MaterialAssetKind::Profile ? source.profile.baseTemplate : source.templateId;
    program.surface = source.surface;
    program.profileAssetGuid = source.profileAssetGuid;
    program.featureRequests = source.featureRequests;
    std::sort(program.featureRequests.begin(), program.featureRequests.end(),
              [](const auto left, const auto right) { return ToString(left) < ToString(right); });
    program.featureRequests.erase(std::unique(program.featureRequests.begin(), program.featureRequests.end()),
                                  program.featureRequests.end());
    if (source.profileAssetGuid.IsValid()) {
        AddCapability(program.capabilities, MaterialProgramCapability::MaterialProfile);
    }
    if (!functionDependencies.empty()) {
        AddCapability(program.capabilities, MaterialProgramCapability::FunctionCalls);
    }
    if (source.surface.unlit) {
        AddCapability(program.capabilities, MaterialProgramCapability::Unlit);
    }
    if (source.surface.doubleSided) {
        AddCapability(program.capabilities, MaterialProgramCapability::DoubleSided);
    }
    if (source.surface.alphaMode == MaterialAlphaMode::Mask) {
        AddCapability(program.capabilities, MaterialProgramCapability::AlphaMask);
    } else if (source.surface.alphaMode == MaterialAlphaMode::Blend) {
        AddCapability(program.capabilities, MaterialProgramCapability::AlphaBlend);
    }

    const MaterialGraphData* graph = source.ActiveGraph();
    if (graph) {
        const std::unordered_set<std::string> reachable = CollectMaterialGraphUpstreamNodes(*graph);
        AddNodeCapabilities(*graph, program.capabilities);
        AddReachableOutlineFeatureRequests(*graph, program.featureRequests);
        std::sort(program.featureRequests.begin(), program.featureRequests.end(),
                  [](const auto left, const auto right) { return ToString(left) < ToString(right); });
        program.featureRequests.erase(std::unique(program.featureRequests.begin(), program.featureRequests.end()),
                                      program.featureRequests.end());
        auto parameters = graph->parameters;
        auto resources = graph->resources;
        RetainReachableBindings(*graph, reachable, parameters, resources);
        std::sort(parameters.begin(), parameters.end(),
                  [](const auto& left, const auto& right) { return left.id < right.id; });
        std::sort(resources.begin(), resources.end(),
                  [](const auto& left, const auto& right) { return left.id < right.id; });
        uint32_t byteOffset = 0u;
        for (const MaterialGraphParameterData& parameter : parameters) {
            program.parameters.push_back(MaterialProgramParameterBinding{
                parameter.id,
                parameter.defaultValue.type,
                byteOffset,
                16u,
                parameter.defaultValue,
            });
            byteOffset += 16u;
        }
        std::vector<const MaterialGraphNodeData*> variableNodes{};
        for (const MaterialGraphNodeData& node : graph->nodes) {
            if (reachable.contains(node.id) && node.kind == MaterialNodeKind::Variable) {
                variableNodes.push_back(&node);
            }
        }
        std::sort(
            variableNodes.begin(), variableNodes.end(),
            [](const MaterialGraphNodeData* left, const MaterialGraphNodeData* right) { return left->id < right->id; });
        for (const MaterialGraphNodeData* node : variableNodes) {
            MaterialValueData fallback = node->value;
            fallback.type = node->valueType;
            program.parameters.push_back(MaterialProgramParameterBinding{
                MakeMaterialVariableParameterId(node->id),
                node->valueType,
                byteOffset,
                16u,
                fallback,
                node->referenceId,
            });
            byteOffset += 16u;
        }
        uint32_t resourceIndex = 0u;
        for (const MaterialGraphResourceData& resource : resources) {
            program.resources.push_back(MaterialProgramResourceBinding{
                resource.id,
                resource.type,
                resource.semantic,
                resource.assetGuid,
                resourceIndex++,
                resource.texCoord,
                resource.uvScale,
                resource.uvOffset,
                resource.uvRotation,
            });
        }
        MATERIAL_COMPILER_INTERNAL::BuildMaterialProgramInstructions(*graph, program);
    } else if (source.kind == MaterialAssetKind::Instance) {
        AddCapability(program.capabilities, MaterialProgramCapability::MaterialInstance);
        program.parentMaterialGuid = source.instance.parentMaterialGuid;
        auto parameters = source.instance.parameterOverrides;
        auto resources = source.instance.resourceOverrides;
        std::sort(parameters.begin(), parameters.end(),
                  [](const auto& left, const auto& right) { return left.parameterId < right.parameterId; });
        std::sort(resources.begin(), resources.end(),
                  [](const auto& left, const auto& right) { return left.resourceId < right.resourceId; });
        uint32_t byteOffset = 0u;
        for (const auto& parameter : parameters) {
            program.parameters.push_back(MaterialProgramParameterBinding{
                parameter.parameterId,
                parameter.value.type,
                byteOffset,
                16u,
                parameter.value,
            });
            byteOffset += 16u;
        }
        uint32_t resourceIndex = 0u;
        for (const auto& resource : resources) {
            program.resources.push_back(MaterialProgramResourceBinding{
                resource.resourceId,
                MaterialResourceType::Texture2D,
                MaterialResourceSemantic::Custom,
                resource.assetGuid,
                resourceIndex++,
                0u,
                {1.0f, 1.0f},
                {0.0f, 0.0f},
                0.0f,
            });
        }
    } else if (source.kind == MaterialAssetKind::Profile) {
        AddCapability(program.capabilities, MaterialProgramCapability::MaterialProfile);
        program.featureRequests = source.profile.featureRequests;
        std::sort(program.featureRequests.begin(), program.featureRequests.end(),
                  [](const auto left, const auto right) { return ToString(left) < ToString(right); });
    } else if (source.kind == MaterialAssetKind::Ramp) {
        AddCapability(program.capabilities, MaterialProgramCapability::RampAsset);
    }

    std::set<std::string> dependencyIds{};
    const auto addDependency = [&](const AssetGuid& guid) {
        if (guid.IsValid() && dependencyIds.emplace(guid.value).second) {
            program.dependencyGuids.push_back(guid);
        }
    };
    if (graph) {
        addDependency(source.profileAssetGuid);
        for (const MaterialProgramResourceBinding& resource : program.resources) {
            addDependency(resource.assetGuid);
        }
        for (const MaterialProgramInstruction& instruction : program.instructions) {
            if (instruction.opcode == MaterialProgramOpcode::FunctionCall) {
                addDependency(instruction.assetReference);
            }
        }
        for (const AssetGuid& dependency : functionDependencies) {
            addDependency(dependency);
        }
    } else {
        for (const MaterialAssetDependency& dependency : CollectMaterialAssetDependencies(source)) {
            addDependency(dependency.assetGuid);
        }
    }

    const std::string structure = BuildStructureDescription(source, program);
    program.key.structureHash = HASH::Fnv1aString(structure);
    program.key.domain = program.domain;
    program.key.capabilityMask = program.capabilities;

    std::string canonicalSource{};
    std::string serializationError{};
    if (MATERIAL_SERIALIZATION::SerializeMaterialCompileInputJson(source, canonicalSource, serializationError)) {
        program.programId =
            TEXT::ToHexString(HASH::Fnv1aString(std::to_string(kMaterialProgramIrVersion) + ":" + canonicalSource));
    } else {
        program.programId = TEXT::ToHexString(program.key.structureHash);
    }
    return program;
}

} // namespace

MaterialProgramIR BuildStandardPbrMaterialProgramFallback() {
    PbrMaterialAssetData pbr{};
    pbr.version = kMaterialAssetSourceVersion;
    pbr.materialName = "HIKARI Standard PBR Fallback";
    return BuildProgram(MakeStandardPbrMaterialAssetData(pbr));
}

bool ResolveMaterialProgramShellSettings(const MaterialProgramIR& program, MaterialShellSettings& outSettings,
                                         std::string& outError,
                                         const std::function<const MaterialValueData*(uint32_t)>& parameterOverride) {
    outSettings = {};
    outError.clear();
    const auto producer = [&](uint32_t output) -> const MaterialProgramInstruction* {
        if (output == kInvalidMaterialProgramRegister)
            return nullptr;
        for (const auto& instruction : program.instructions)
            if (std::find(instruction.outputRegisters.begin(), instruction.outputRegisters.end(), output) !=
                instruction.outputRegisters.end())
                return &instruction;
        return nullptr;
    };
    const MaterialProgramInstruction* shell = nullptr;
    for (const auto& instruction : program.instructions) {
        if (instruction.opcode != MaterialProgramOpcode::PbrSurface)
            continue;
        const uint32_t shellRegister = FindMaterialProgramInputRegister(instruction, "Shell");
        if (shellRegister == kInvalidMaterialProgramRegister)
            continue;
        shell = producer(shellRegister);
        if (shell == nullptr || shell->opcode != MaterialProgramOpcode::Shell) {
            outError = "PBR Shell requires a Shell configuration node";
            return false;
        }
        break;
    }
    if (shell == nullptr)
        return true;
    if (program.domain != MaterialDomain::Surface || program.surface.alphaMode != MaterialAlphaMode::Mask) {
        outError = "Shell geometry requires the Surface domain and Mask alpha mode";
        return false;
    }
    bool valid = true;
    kMaterialShellFields([&](const char* pin, auto member, JSONFIELDS::Range range) {
        if (!valid)
            return;
        const uint32_t input = FindMaterialProgramInputRegister(*shell, pin);
        if (input == kInvalidMaterialProgramRegister)
            return;
        const auto* valueInstruction = producer(input);
        const MaterialValueData* value = nullptr;
        if (valueInstruction && valueInstruction->opcode == MaterialProgramOpcode::Constant) {
            value = &valueInstruction->immediate;
        } else if (valueInstruction && valueInstruction->opcode == MaterialProgramOpcode::Parameter &&
                   valueInstruction->bindingIndex < program.parameters.size()) {
            value = parameterOverride ? parameterOverride(valueInstruction->bindingIndex) : nullptr;
            if (value == nullptr)
                value = &program.parameters[valueInstruction->bindingIndex].defaultValue;
        }
        if (value == nullptr || value->type != MaterialValueType::Float1 || !MATH::IsFinite(value->numeric.x) ||
            value->numeric.x < range.min || value->numeric.x > range.max) {
            valid = false;
            outError = std::string("Shell ") + pin + " requires a finite in-range scalar constant or parameter";
            return;
        }
        using Field = std::remove_reference_t<decltype(outSettings.*member)>;
        if constexpr (std::is_integral_v<Field>) {
            if (value->numeric.x != std::floor(value->numeric.x)) {
                valid = false;
                outError = "Shell MaxLayers must be 0, 2, 4, or 8";
                return;
            }
        }
        outSettings.*member = static_cast<Field>(value->numeric.x);
    });
    if (valid && !IsValidMaterialShellSettings(outSettings)) {
        valid = false;
        outError = "Shell requires 0/2/4/8 layers and ordered projected-pixel LOD thresholds";
    }
    return valid;
}

MaterialCompileResult CompileMaterialAsset(const MaterialAssetData& source) { return CompileMaterialAsset(source, {}); }

uint32_t FindMaterialProgramInputRegister(const MaterialProgramInstruction& instruction,
                                          std::string_view pin) noexcept {
    const auto found = std::find(instruction.inputPins.begin(), instruction.inputPins.end(), pin);
    const size_t index = static_cast<size_t>(found - instruction.inputPins.begin());
    return found != instruction.inputPins.end() && index < instruction.inputRegisters.size()
               ? instruction.inputRegisters[index]
               : kInvalidMaterialProgramRegister;
}

MaterialCompileResult CompileMaterialAsset(const MaterialAssetData& source, const MaterialCompileOptions& options) {
    MaterialCompileResult result{};
    MaterialAssetData expanded{};
    std::vector<AssetGuid> functionDependencies{};
    const bool functionsExpanded =
        MATERIAL_COMPILER_INTERNAL::ExpandMaterialFunctions(source, options, expanded, functionDependencies, result);
    if (functionsExpanded) {
        MaterialGraphData* graph = expanded.kind == MaterialAssetKind::Material   ? &expanded.graph
                                   : expanded.kind == MaterialAssetKind::Function ? &expanded.function.graph
                                                                                  : nullptr;
        if (graph != nullptr) {
            (void)ReconcileMaterialGraphDynamicNodeTypes(*graph);
        }
    }
    if (functionsExpanded && MATERIAL_COMPILER_INTERNAL::ValidateMaterialSource(expanded, result)) {
        result.program = BuildProgram(expanded, functionDependencies);
        std::string shellError;
        if (ResolveMaterialProgramShellSettings(result.program, result.program.shell, shellError)) {
            result.success = true;
            return result;
        }
        AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Error, "Material.InvalidShell", std::move(shellError));
    }

    result.usedFallback = true;
    result.program = BuildStandardPbrMaterialProgramFallback();
    AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Warning, "Material.UsingStandardPbrFallback",
                  "material compilation failed; using the deterministic Standard PBR fallback");
    return result;
}

} // namespace HIKARI
