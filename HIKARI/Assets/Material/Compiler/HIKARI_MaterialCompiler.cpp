#include "HIKARI_MaterialCompiler.h"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <unordered_set>
#include <utility>

#include "Assets/Material/Compiler/Internal/HIKARI_MaterialCompilerValidation.h"
#include "Assets/Material/Compiler/Internal/HIKARI_MaterialProgramLowering.h"
#include "Assets/Material/Authoring/HIKARI_MaterialSemanticNodeFactory.h"
#include "Assets/Material/HIKARI_MaterialDependencies.h"
#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"

namespace HIKARI {

    namespace {

        void AddDiagnostic(
            MaterialCompileResult& result,
            MaterialCompileDiagnosticSeverity severity,
            std::string code,
            std::string message,
            std::string subjectId = {}) {

            result.diagnostics.push_back(MaterialCompileDiagnostic{
                severity,
                std::move(code),
                std::move(message),
                std::move(subjectId)
            });
        }

        uint64_t HashText(std::string_view text) noexcept {
            uint64_t hash = 14695981039346656037ull;
            for (const unsigned char value : text) {
                hash ^= static_cast<uint64_t>(value);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::string HashToString(uint64_t hash) {
            std::ostringstream stream{};
            stream << std::hex << std::setfill('0') << std::setw(16) << hash;
            return stream.str();
        }

        void AddCapability(
            uint64_t& mask,
            MaterialProgramCapability capability) noexcept {
            mask |= MaterialCapabilityBit(capability);
        }

        std::unordered_set<std::string> CollectReachableNodeIds(
            const MaterialGraphData& graph) {
            std::unordered_set<std::string> reachable{};
            std::vector<std::string> pending{};
            if (!graph.outputNodeId.empty()) {
                pending.push_back(graph.outputNodeId);
            }
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

        struct ReachableBindingIds {
            std::unordered_set<std::string> parameters{};
            std::unordered_set<std::string> resources{};
        };

        ReachableBindingIds CollectReachableBindingIds(
            const MaterialGraphData& graph,
            const std::unordered_set<std::string>& reachable) {
            ReachableBindingIds bindings{};
            for (const MaterialGraphNodeData& node : graph.nodes) {
                if (!reachable.contains(node.id)) {
                    continue;
                }
                if (node.kind == MaterialNodeKind::Parameter &&
                    !node.referenceId.empty()) {
                    bindings.parameters.emplace(node.referenceId);
                }
                if (node.kind == MaterialNodeKind::TextureSample &&
                    !node.referenceId.empty()) {
                    bindings.resources.emplace(node.referenceId);
                }
                for (const std::string_view id :
                     ASSETS::MATERIAL::GetMaterialNodeOwnedParameterIds(
                         node.kind)) {
                    bindings.parameters.emplace(id);
                }
                for (const std::string_view id :
                     ASSETS::MATERIAL::GetMaterialNodeOwnedResourceIds(
                         node.kind)) {
                    bindings.resources.emplace(id);
                }
            }
            return bindings;
        }

        void RetainReachableBindings(
            const MaterialGraphData& graph,
            const std::unordered_set<std::string>& reachable,
            std::vector<MaterialGraphParameterData>& parameters,
            std::vector<MaterialGraphResourceData>& resources) {
            const ReachableBindingIds bindings =
                CollectReachableBindingIds(graph, reachable);
            std::erase_if(parameters, [&bindings](const auto& parameter) {
                return !bindings.parameters.contains(parameter.id);
            });
            std::erase_if(resources, [&bindings](const auto& resource) {
                return !bindings.resources.contains(resource.id);
            });
        }

        void AddNodeCapabilities(
            const MaterialGraphData& graph,
            uint64_t& capabilities) {

            const std::unordered_set<std::string> reachable =
                CollectReachableNodeIds(graph);

            for (const MaterialGraphNodeData& node : graph.nodes) {
                if (!reachable.contains(node.id)) {
                    continue;
                }
                switch (node.kind) {
                case MaterialNodeKind::PbrSurface:
                    AddCapability(capabilities, MaterialProgramCapability::StandardPbr);
                    break;
                case MaterialNodeKind::RampDiffuse:
                    AddCapability(capabilities, MaterialProgramCapability::RampDiffuse);
                    break;
                case MaterialNodeKind::MultiRampDiffuse:
                    AddCapability(capabilities, MaterialProgramCapability::MultiRampDiffuse);
                    break;
                case MaterialNodeKind::SpecularBand:
                    AddCapability(capabilities, MaterialProgramCapability::SpecularBand);
                    break;
                case MaterialNodeKind::FresnelRim:
                    AddCapability(capabilities, MaterialProgramCapability::FresnelRim);
                    break;
                case MaterialNodeKind::MatCap:
                    AddCapability(capabilities, MaterialProgramCapability::MatCap);
                    break;
                case MaterialNodeKind::AnisotropicHair:
                    AddCapability(capabilities, MaterialProgramCapability::AnisotropicHair);
                    break;
                case MaterialNodeKind::FaceSdf:
                    AddCapability(capabilities, MaterialProgramCapability::FaceSdf);
                    break;
                case MaterialNodeKind::EyeLighting:
                    AddCapability(capabilities, MaterialProgramCapability::EyeLighting);
                    break;
                case MaterialNodeKind::SkinLighting:
                    AddCapability(capabilities, MaterialProgramCapability::SkinLighting);
                    break;
                case MaterialNodeKind::FabricSheen:
                    AddCapability(capabilities, MaterialProgramCapability::FabricSheen);
                    break;
                case MaterialNodeKind::HybridLighting:
                    AddCapability(capabilities, MaterialProgramCapability::HybridLighting);
                    break;
                case MaterialNodeKind::Outline:
                    AddCapability(capabilities, MaterialProgramCapability::Outline);
                    break;
                case MaterialNodeKind::HairShadow:
                    AddCapability(capabilities, MaterialProgramCapability::HairShadow);
                    break;
                case MaterialNodeKind::FunctionCall:
                    AddCapability(capabilities, MaterialProgramCapability::FunctionCalls);
                    break;
                default:
                    break;
                }
            }
        }

        void AddReachableOutlineFeatureRequests(
            const MaterialGraphData& graph,
            std::vector<MaterialFeatureRequest>& requests) {
            const std::unordered_set<std::string> reachable =
                CollectReachableNodeIds(graph);
            const bool hasOutline = std::any_of(
                graph.nodes.begin(), graph.nodes.end(),
                [&reachable](const MaterialGraphNodeData& node) {
                    return node.kind == MaterialNodeKind::Outline &&
                        reachable.contains(node.id);
                });
            std::erase_if(requests, [](MaterialFeatureRequest request) {
                return request == MaterialFeatureRequest::GeometryOutline ||
                    request == MaterialFeatureRequest::ScreenOutline ||
                    // The legacy screen-space FaceHairShadow flag has no
                    // authored texture node and therefore cannot be inferred
                    // from the connected graph. Do not retain it as hidden
                    // template behavior.
                    request == MaterialFeatureRequest::FaceHairShadow;
            });
            if (!hasOutline) {
                return;
            }
            float mode = 1.0f;
            const auto parameter = std::find_if(
                graph.parameters.begin(), graph.parameters.end(),
                [](const MaterialGraphParameterData& value) {
                    return value.id == "parameter.outlineMode";
                });
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

        const MaterialGraphData* SelectGraph(const MaterialAssetData& source) noexcept {
            if (source.kind == MaterialAssetKind::Material) {
                return &source.graph;
            }
            if (source.kind == MaterialAssetKind::Function) {
                return &source.function.graph;
            }
            return nullptr;
        }

        std::string BuildStructureDescription(
            const MaterialAssetData& source,
            const MaterialProgramIR& program) {

            std::ostringstream stream{};
            stream << ToString(source.kind) << ':'
                << ToString(source.domain) << ':'
                << ToString(program.templateId) << ':'
                << source.profileAssetGuid.value << ':'
                << ToString(source.surface.alphaMode) << ':'
                << source.surface.doubleSided << ':'
                << source.surface.unlit << ':'
                << program.capabilities;
            for (const MaterialFeatureRequest feature :
                 program.featureRequests) {
                stream << "|F:" << ToString(feature);
            }
            if (const MaterialGraphData* graph = SelectGraph(source)) {
                const std::unordered_set<std::string> reachable =
                    CollectReachableNodeIds(*graph);
                auto nodes = graph->nodes;
                auto edges = graph->edges;
                auto parameters = graph->parameters;
                auto resources = graph->resources;
                std::erase_if(nodes, [&reachable](const auto& node) {
                    return !reachable.contains(node.id);
                });
                std::erase_if(edges, [&reachable](const auto& edge) {
                    return !reachable.contains(edge.sourceNodeId) ||
                        !reachable.contains(edge.targetNodeId);
                });
                RetainReachableBindings(
                    *graph, reachable, parameters, resources);
                std::sort(nodes.begin(), nodes.end(), [](const auto& left, const auto& right) {
                    return left.id < right.id;
                });
                std::sort(edges.begin(), edges.end(), [](const auto& left, const auto& right) {
                    return left.id < right.id;
                });
                std::sort(parameters.begin(), parameters.end(), [](const auto& left, const auto& right) {
                    return left.id < right.id;
                });
                std::sort(resources.begin(), resources.end(), [](const auto& left, const auto& right) {
                    return left.id < right.id;
                });
                for (const auto& node : nodes) {
                    stream << "|N:" << node.id << ':' << ToString(node.kind) << ':'
                        << ToString(node.valueType) << ':' << node.referenceId << ':'
                        << node.assetReference.value;
                    auto properties = node.properties;
                    std::sort(
                        properties.begin(),
                        properties.end(),
                        [](const auto& left, const auto& right) {
                            return left.id < right.id;
                        });
                    for (const auto& property : properties) {
                        stream << ":Property(" << property.id << ','
                            << ToString(property.value.type) << ')';
                    }
                }
                for (const auto& edge : edges) {
                    stream << "|E:" << edge.sourceNodeId << ':' << edge.sourcePin
                        << ':' << edge.targetNodeId << ':' << edge.targetPin;
                }
                for (const auto& parameter : parameters) {
                    stream << "|P:" << parameter.id << ':'
                        << ToString(parameter.defaultValue.type);
                }
                for (const auto& resource : resources) {
                    stream << "|R:" << resource.id << ':'
                        << ToString(resource.type) << ':'
                        << ToString(resource.semantic);
                }
            }
            return stream.str();
        }

        MaterialProgramIR BuildProgram(const MaterialAssetData& source) {
            MaterialProgramIR program{};
            program.sourceKind = source.kind;
            program.domain = source.domain;
            program.templateId = source.kind == MaterialAssetKind::Profile
                ? source.profile.baseTemplate
                : source.templateId;
            program.surface = source.surface;
            program.profileAssetGuid = source.profileAssetGuid;
            program.featureRequests = source.featureRequests;
            std::sort(program.featureRequests.begin(), program.featureRequests.end(),
                [](const auto left, const auto right) {
                    return ToString(left) < ToString(right);
                });
            program.featureRequests.erase(
                std::unique(
                    program.featureRequests.begin(),
                    program.featureRequests.end()),
                program.featureRequests.end());
            if (source.profileAssetGuid.IsValid()) {
                AddCapability(program.capabilities,
                    MaterialProgramCapability::MaterialProfile);
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

            const MaterialGraphData* graph = SelectGraph(source);
            if (graph) {
                const std::unordered_set<std::string> reachable =
                    CollectReachableNodeIds(*graph);
                AddNodeCapabilities(*graph, program.capabilities);
                AddReachableOutlineFeatureRequests(
                    *graph, program.featureRequests);
                std::sort(program.featureRequests.begin(), program.featureRequests.end(),
                    [](const auto left, const auto right) {
                        return ToString(left) < ToString(right);
                    });
                program.featureRequests.erase(
                    std::unique(program.featureRequests.begin(),
                                program.featureRequests.end()),
                    program.featureRequests.end());
                auto parameters = graph->parameters;
                auto resources = graph->resources;
                RetainReachableBindings(
                    *graph, reachable, parameters, resources);
                std::sort(parameters.begin(), parameters.end(), [](const auto& left, const auto& right) {
                    return left.id < right.id;
                });
                std::sort(resources.begin(), resources.end(), [](const auto& left, const auto& right) {
                    return left.id < right.id;
                });
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
                MATERIAL_COMPILER_INTERNAL::BuildMaterialProgramInstructions(
                    *graph, program);
            } else if (source.kind == MaterialAssetKind::Instance) {
                AddCapability(program.capabilities, MaterialProgramCapability::MaterialInstance);
                program.parentMaterialGuid = source.instance.parentMaterialGuid;
                auto parameters = source.instance.parameterOverrides;
                auto resources = source.instance.resourceOverrides;
                std::sort(parameters.begin(), parameters.end(), [](const auto& left, const auto& right) {
                    return left.parameterId < right.parameterId;
                });
                std::sort(resources.begin(), resources.end(), [](const auto& left, const auto& right) {
                    return left.resourceId < right.resourceId;
                });
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
                        { 1.0f, 1.0f },
                        { 0.0f, 0.0f },
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
                if (guid.IsValid() &&
                    dependencyIds.emplace(guid.value).second) {
                    program.dependencyGuids.push_back(guid);
                }
            };
            if (graph) {
                addDependency(source.profileAssetGuid);
                for (const MaterialProgramResourceBinding& resource :
                     program.resources) {
                    addDependency(resource.assetGuid);
                }
                for (const MaterialProgramInstruction& instruction :
                     program.instructions) {
                    if (instruction.opcode ==
                        MaterialProgramOpcode::FunctionCall) {
                        addDependency(instruction.assetReference);
                    }
                }
            } else {
                for (const MaterialAssetDependency& dependency :
                     CollectMaterialAssetDependencies(source)) {
                    addDependency(dependency.assetGuid);
                }
            }

            const std::string structure = BuildStructureDescription(source, program);
            program.key.structureHash = HashText(structure);
            program.key.domain = program.domain;
            program.key.capabilityMask = program.capabilities;

            std::string canonicalSource{};
            std::string serializationError{};
            if (MATERIAL_SERIALIZATION::SerializeMaterialSourceJson(
                    source, canonicalSource, serializationError)) {
                program.programId = HashToString(HashText(canonicalSource));
            } else {
                program.programId = HashToString(program.key.structureHash);
            }
            return program;
        }

        bool IsCompatibleLastGood(
            const MaterialAssetData& source,
            const MaterialProgramIR* lastGood) noexcept {

            return lastGood &&
                lastGood->version == kMaterialProgramIrVersion &&
                lastGood->domain == source.domain &&
                lastGood->sourceKind == source.kind;
        }

    } // namespace

    MaterialProgramIR BuildStandardPbrMaterialProgramFallback() {
        PbrMaterialAssetData pbr{};
        pbr.version = kMaterialAssetSourceVersion;
        pbr.materialName = "HIKARI Standard PBR Fallback";
        return BuildProgram(MakeStandardPbrMaterialAssetData(pbr));
    }

    MaterialCompileResult CompileMaterialAsset(
        const MaterialAssetData& source,
        const MaterialProgramIR* lastGoodProgram) {

        MaterialCompileResult result{};
        if (MATERIAL_COMPILER_INTERNAL::ValidateMaterialSource(source, result)) {
            result.success = true;
            result.program = BuildProgram(source);
            if (source.migratedFromLegacy) {
                AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Info,
                    "Material.LegacyMigration",
                    "legacy PBR material was compiled through the Material v4 StandardPBR adapter");
            }
            return result;
        }

        if (IsCompatibleLastGood(source, lastGoodProgram)) {
            result.usedLastGood = true;
            result.program = *lastGoodProgram;
            AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Warning,
                "Material.UsingLastGood",
                "material compilation failed; retained the compatible last-good program");
        } else {
            result.usedFallback = true;
            result.program = BuildStandardPbrMaterialProgramFallback();
            AddDiagnostic(result, MaterialCompileDiagnosticSeverity::Warning,
                "Material.UsingStandardPbrFallback",
                "material compilation failed without a compatible last-good program; using Standard PBR fallback");
        }
        return result;
    }

} // namespace HIKARI
