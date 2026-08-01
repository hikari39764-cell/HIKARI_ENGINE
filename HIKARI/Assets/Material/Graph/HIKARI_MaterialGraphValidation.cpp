#include "HIKARI_MaterialGraphValidation.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace HIKARI {
    namespace {
        constexpr size_t kMaxNodes = 512u;
        constexpr size_t kMaxEdges = 2048u;
        constexpr size_t kMaxParameters = 256u;
        constexpr size_t kMaxResources = 256u;
        struct NodeSignature final {
            std::vector<std::pair<std::string_view, MaterialValueType>> inputs{};
            std::vector<std::pair<std::string_view, MaterialValueType>> outputs{};
            bool acceptsDynamicNumericInputs = false;
            bool emitsDynamicNumericOutputs = false;
        };
        void AddDiagnostic(
            std::vector<MaterialGraphDiagnostic>& diagnostics,
            MaterialGraphDiagnosticSeverity severity,
            std::string code,
            std::string message,
            std::string subjectId = {}) {
            diagnostics.push_back(MaterialGraphDiagnostic{
                severity,
                std::move(code),
                std::move(message),
                std::move(subjectId)
            });
        }
        bool ContainsLightmapName(std::string_view text) {
            std::string folded{};
            folded.reserve(text.size());
            for (const char c : text) {
                if (c >= 'A' && c <= 'Z') {
                    folded.push_back(static_cast<char>(c - 'A' + 'a'));
                } else if (c != '_' && c != '-' && c != ' ') {
                    folded.push_back(c);
                }
            }
            return folded.find("lightmap") != std::string::npos;
        }
        bool IsFiniteValue(const MaterialValueData& value) noexcept {
            if (!IsMaterialNumericType(value.type)) {
                return true;
            }
            return std::isfinite(value.numeric.x) &&
                std::isfinite(value.numeric.y) &&
                std::isfinite(value.numeric.z) &&
                std::isfinite(value.numeric.w);
        }
        NodeSignature MakeSignature(
            const MaterialGraphNodeData& node,
            const std::unordered_map<std::string, MaterialValueType>& parameters) {
            switch (node.kind) {
            case MaterialNodeKind::Constant:
                return { {}, { { "Value", node.value.type } } };
            case MaterialNodeKind::Parameter: {
                const auto parameter = parameters.find(node.referenceId);
                const MaterialValueType type = parameter == parameters.end()
                    ? node.valueType
                    : parameter->second;
                return { {}, { { "Value", type } } };
            }
            case MaterialNodeKind::TextureSample:
                return { {}, {
                    { "Color", MaterialValueType::Float4 },
                    { "RGB", MaterialValueType::Float3 },
                    { "Alpha", MaterialValueType::Float1 }
                } };
            case MaterialNodeKind::Add:
            case MaterialNodeKind::Multiply:
                return { { { "A", MaterialValueType::Float1 }, { "B", MaterialValueType::Float1 } },
                    { { "Result", node.valueType } }, true, true };
            case MaterialNodeKind::Lerp:
                return { {
                    { "A", MaterialValueType::Float1 },
                    { "B", MaterialValueType::Float1 },
                    { "Alpha", MaterialValueType::Float1 }
                }, { { "Result", node.valueType } }, true, true };
            case MaterialNodeKind::Clamp:
                return { {
                    { "Value", MaterialValueType::Float1 },
                    { "Min", MaterialValueType::Float1 },
                    { "Max", MaterialValueType::Float1 }
                }, { { "Result", node.valueType } }, true, true };
            case MaterialNodeKind::Saturate:
            case MaterialNodeKind::OneMinus:
                return { { { "Value", MaterialValueType::Float1 } },
                    { { "Result", node.valueType } }, true, true };
            case MaterialNodeKind::Power:
                return { { { "Base", MaterialValueType::Float1 }, { "Exponent", MaterialValueType::Float1 } },
                    { { "Result", node.valueType } }, true, true };
            case MaterialNodeKind::Dot:
                return { { { "A", MaterialValueType::Float3 }, { "B", MaterialValueType::Float3 } },
                    { { "Result", MaterialValueType::Float1 } }, true, false };
            case MaterialNodeKind::NormalUnpack:
                return { { { "PackedNormal", MaterialValueType::Float3 } },
                    { { "Normal", MaterialValueType::Float3 } } };
            case MaterialNodeKind::PbrSurface:
                return { {
                    { "BaseColor", MaterialValueType::Float4 },
                    { "Normal", MaterialValueType::Float3 },
                    { "Metallic", MaterialValueType::Float1 },
                    { "Roughness", MaterialValueType::Float1 },
                    { "Occlusion", MaterialValueType::Float1 },
                    { "Emissive", MaterialValueType::Float3 },
                    { "Specular", MaterialValueType::Float1 },
                    { "SpecularColor", MaterialValueType::Float3 },
                    { "DiffuseResponse", MaterialValueType::DiffuseResponse },
                    { "SpecularResponse", MaterialValueType::SpecularResponse },
                    { "Additive", MaterialValueType::Float3 }
                }, { { "Surface", MaterialValueType::Surface } } };
            case MaterialNodeKind::RampDiffuse:
            case MaterialNodeKind::MultiRampDiffuse:
                return { {
                    { "NdotL", MaterialValueType::Float1 },
                    { "Shadow", MaterialValueType::Float1 },
                    { "Mask", MaterialValueType::Float1 }
                }, { { "Response", MaterialValueType::DiffuseResponse } } };
            case MaterialNodeKind::SpecularBand:
                return { {
                    { "NdotH", MaterialValueType::Float1 },
                    { "Width", MaterialValueType::Float1 },
                    { "Softness", MaterialValueType::Float1 }
                }, { { "Response", MaterialValueType::SpecularResponse } } };
            case MaterialNodeKind::FresnelRim:
                return { {
                    { "NdotV", MaterialValueType::Float1 },
                    { "Power", MaterialValueType::Float1 },
                    { "Width", MaterialValueType::Float1 },
                    { "Color", MaterialValueType::Float3 }
                }, { { "Color", MaterialValueType::Float3 } } };
            case MaterialNodeKind::MatCap:
                return { {
                    { "Normal", MaterialValueType::Float3 },
                    { "ViewDirection", MaterialValueType::Float3 },
                    { "Mask", MaterialValueType::Float1 }
                }, { { "Color", MaterialValueType::Float3 } } };
            case MaterialNodeKind::AnisotropicHair:
                return { {
                    { "Tangent", MaterialValueType::Float3 },
                    { "Shift", MaterialValueType::Float1 },
                    { "Width", MaterialValueType::Float1 }
                }, { { "Response", MaterialValueType::SpecularResponse } } };
            case MaterialNodeKind::FaceSdf:
                return { {
                    { "LightDirection", MaterialValueType::Float3 },
                    { "SDF", MaterialValueType::Float1 },
                    { "Softness", MaterialValueType::Float1 }
                }, { { "Response", MaterialValueType::DiffuseResponse } } };
            case MaterialNodeKind::EyeLighting:
                return { {
                    { "BaseColor", MaterialValueType::Float3 },
                    { "Mask", MaterialValueType::Float1 },
                    { "ViewDirection", MaterialValueType::Float3 }
                }, { { "Color", MaterialValueType::Float3 } } };
            case MaterialNodeKind::SurfaceOutput:
                return { { { "Surface", MaterialValueType::Surface } }, {} };
            case MaterialNodeKind::FunctionCall:
                return { { { "Input", node.valueType } }, { { "Result", node.valueType } } };
            }
            return {};
        }

        const std::pair<std::string_view, MaterialValueType>* FindPin(
            const std::vector<std::pair<std::string_view, MaterialValueType>>& pins,
            std::string_view name) {
            const auto result = std::find_if(
                pins.begin(),
                pins.end(),
                [name](const auto& pin) { return pin.first == name; });
            return result == pins.end() ? nullptr : &*result;
        }

        bool ArePinTypesCompatible(
            MaterialValueType source,
            MaterialValueType target,
            bool targetAcceptsNumeric) noexcept {
            if (source == target) {
                return true;
            }
            if (!IsMaterialNumericType(source) || !IsMaterialNumericType(target)) {
                return false;
            }
            return targetAcceptsNumeric ||
                source == MaterialValueType::Float1;
        }

        bool IsDynamicNumericInput(
            MaterialNodeKind kind,
            std::string_view pin) noexcept {
            switch (kind) {
            case MaterialNodeKind::Add:
            case MaterialNodeKind::Multiply:
                return pin == "A" || pin == "B";
            case MaterialNodeKind::Lerp:
                return pin == "A" || pin == "B";
            case MaterialNodeKind::Clamp:
            case MaterialNodeKind::Saturate:
            case MaterialNodeKind::OneMinus:
                return pin == "Value";
            case MaterialNodeKind::Power:
                return pin == "Base";
            default:
                return false;
            }
        }

        bool HasCycle(
            const MaterialGraphData& graph,
            const std::unordered_map<std::string, size_t>& nodeIndices) {
            std::vector<std::vector<size_t>> adjacency(graph.nodes.size());
            for (const MaterialGraphEdgeData& edge : graph.edges) {
                const auto source = nodeIndices.find(edge.sourceNodeId);
                const auto target = nodeIndices.find(edge.targetNodeId);
                if (source != nodeIndices.end() && target != nodeIndices.end()) {
                    adjacency[source->second].push_back(target->second);
                }
            }

            std::vector<uint8_t> state(graph.nodes.size(), 0u);
            std::function<bool(size_t)> visit = [&](size_t index) {
                if (state[index] == 1u) {
                    return true;
                }
                if (state[index] == 2u) {
                    return false;
                }
                state[index] = 1u;
                for (const size_t next : adjacency[index]) {
                    if (visit(next)) {
                        return true;
                    }
                }
                state[index] = 2u;
                return false;
            };

            for (size_t index = 0; index < graph.nodes.size(); ++index) {
                if (visit(index)) {
                    return true;
                }
            }
            return false;
        }

    } // namespace

    bool IsValidMaterialStableId(std::string_view id) noexcept {
        if (id.empty() || id.size() > 128u) {
            return false;
        }
        return std::all_of(id.begin(), id.end(), [](const char c) {
            return (c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '_' || c == '-' || c == '.' || c == ':';
        });
    }

    bool ValidateMaterialGraph(
        const MaterialGraphData& graph,
        const MaterialGraphValidationOptions& options,
        std::vector<MaterialGraphDiagnostic>& outDiagnostics) {

        outDiagnostics.clear();
        if (graph.nodes.size() > kMaxNodes ||
            graph.edges.size() > kMaxEdges ||
            graph.parameters.size() > kMaxParameters ||
            graph.resources.size() > kMaxResources) {
            AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                "Material.GraphLimitExceeded",
                "material graph exceeds the bounded v1 node, edge, parameter, or resource limit");
        }

        std::unordered_map<std::string, MaterialValueType> parameters{};
        for (const MaterialGraphParameterData& parameter : graph.parameters) {
            if (!IsValidMaterialStableId(parameter.id) ||
                !parameters.emplace(parameter.id, parameter.defaultValue.type).second) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.InvalidParameterId",
                    "material parameter IDs must be stable and unique", parameter.id);
            }
            if (!IsFiniteValue(parameter.defaultValue)) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.NonFiniteParameter",
                    "material parameter default contains a non-finite value", parameter.id);
            }
            if (parameter.defaultValue.type != MaterialValueType::Bool &&
                !IsMaterialNumericType(parameter.defaultValue.type)) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.InvalidParameterType",
                    "material parameters cannot use connection-only value types",
                    parameter.id);
            }
        }

        std::unordered_set<std::string> resources{};
        for (const MaterialGraphResourceData& resource : graph.resources) {
            if (!IsValidMaterialStableId(resource.id) ||
                !resources.emplace(resource.id).second) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.InvalidResourceId",
                    "material resource IDs must be stable and unique", resource.id);
            }
            if (ContainsLightmapName(resource.id) ||
                ContainsLightmapName(resource.displayName)) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.LightmapOwnedByEngine",
                    "Lightmap is engine-owned indirect lighting and cannot be a material graph resource",
                    resource.id);
            }
            if (resource.texCoord > 3u ||
                !std::isfinite(resource.uvScale.x) ||
                !std::isfinite(resource.uvScale.y) ||
                !std::isfinite(resource.uvOffset.x) ||
                !std::isfinite(resource.uvOffset.y) ||
                !std::isfinite(resource.uvRotation)) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.InvalidResourceTransform",
                    "material resource UV selection and transform must be finite and bounded",
                    resource.id);
            }
            if (resource.enabled && !resource.assetGuid.IsValid()) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Warning,
                    "Material.MissingResourceAsset",
                    "enabled material resource has no asset GUID and will use its semantic fallback",
                    resource.id);
            }
        }

        std::unordered_map<std::string, size_t> nodeIndices{};
        for (size_t index = 0; index < graph.nodes.size(); ++index) {
            const MaterialGraphNodeData& node = graph.nodes[index];
            if (!IsValidMaterialStableId(node.id) ||
                !nodeIndices.emplace(node.id, index).second) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.InvalidNodeId",
                    "material node IDs must be stable and unique", node.id);
            }
            if (!std::isfinite(node.position.x) || !std::isfinite(node.position.y) ||
                !IsFiniteValue(node.value)) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.InvalidNodeValue",
                    "material node position and value must be finite", node.id);
            }
            if (node.kind == MaterialNodeKind::Parameter &&
                parameters.find(node.referenceId) == parameters.end()) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.MissingParameter",
                    "parameter node references an undeclared parameter", node.id);
            }
            if (node.kind == MaterialNodeKind::Constant &&
                node.value.type != MaterialValueType::Bool &&
                !IsMaterialNumericType(node.value.type)) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.InvalidConstantType",
                    "constant nodes cannot create connection-only value types", node.id);
            }
            if ((node.kind == MaterialNodeKind::Add ||
                    node.kind == MaterialNodeKind::Multiply ||
                    node.kind == MaterialNodeKind::Lerp ||
                    node.kind == MaterialNodeKind::Clamp ||
                    node.kind == MaterialNodeKind::Saturate ||
                    node.kind == MaterialNodeKind::OneMinus ||
                    node.kind == MaterialNodeKind::Power) &&
                !IsMaterialNumericType(node.valueType)) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.InvalidMathType",
                    "material math nodes require a numeric valueType", node.id);
            }
            if (node.kind == MaterialNodeKind::TextureSample &&
                resources.find(node.referenceId) == resources.end()) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.MissingResource",
                    "texture sample node references an undeclared resource", node.id);
            }
            if (node.kind == MaterialNodeKind::FunctionCall &&
                !node.assetReference.IsValid()) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.MissingFunction",
                    "function call node requires a Material Function asset GUID", node.id);
            }
            std::unordered_set<std::string> propertyIds{};
            for (const MaterialNodePropertyData& property : node.properties) {
                if (!IsValidMaterialStableId(property.id) ||
                    !propertyIds.emplace(property.id).second ||
                    !IsFiniteValue(property.value)) {
                    AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                        "Material.InvalidNodeProperty",
                        "node property IDs and values must be stable, unique, and finite",
                        node.id);
                }
            }
        }

        if (options.requireSurfaceOutput) {
            const auto output = nodeIndices.find(graph.outputNodeId);
            if (output == nodeIndices.end() ||
                graph.nodes[output->second].kind != MaterialNodeKind::SurfaceOutput) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.MissingSurfaceOutput",
                    "surface material graph requires an explicit SurfaceOutput node",
                    graph.outputNodeId);
            }
        } else if (!graph.outputNodeId.empty() &&
            nodeIndices.find(graph.outputNodeId) == nodeIndices.end()) {
            AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                "Material.MissingGraphOutput",
                "material graph outputNodeId does not reference a node",
                graph.outputNodeId);
        }

        std::unordered_set<std::string> edgeIds{};
        std::unordered_set<std::string> targetPins{};
        for (const MaterialGraphEdgeData& edge : graph.edges) {
            if (!IsValidMaterialStableId(edge.id) ||
                !edgeIds.emplace(edge.id).second) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.InvalidEdgeId",
                    "material edge IDs must be stable and unique", edge.id);
            }
            const auto source = nodeIndices.find(edge.sourceNodeId);
            const auto target = nodeIndices.find(edge.targetNodeId);
            if (source == nodeIndices.end() || target == nodeIndices.end()) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.DanglingEdge",
                    "material edge references a missing node", edge.id);
                continue;
            }
            if (source == target) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.SelfEdge",
                    "material node cannot connect to itself", edge.id);
                continue;
            }
            const std::string targetKey = edge.targetNodeId + ":" + edge.targetPin;
            if (!targetPins.emplace(targetKey).second) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.MultipleInputEdges",
                    "a material input pin accepts exactly one incoming edge", edge.id);
            }

            const NodeSignature sourceSignature =
                MakeSignature(graph.nodes[source->second], parameters);
            const NodeSignature targetSignature =
                MakeSignature(graph.nodes[target->second], parameters);
            const auto* sourcePin = FindPin(sourceSignature.outputs, edge.sourcePin);
            const auto* targetPin = FindPin(targetSignature.inputs, edge.targetPin);
            if (!sourcePin || !targetPin) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.UnknownPin",
                    "material edge references a pin outside the bounded node catalogue", edge.id);
                continue;
            }
            if (!ArePinTypesCompatible(
                sourcePin->second,
                targetPin->second,
                IsDynamicNumericInput(
                    graph.nodes[target->second].kind,
                    edge.targetPin))) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.PinTypeMismatch",
                    "material edge connects incompatible typed pins", edge.id);
            }
        }

        if (options.requireSurfaceOutput &&
            !graph.outputNodeId.empty() &&
            targetPins.find(graph.outputNodeId + ":Surface") == targetPins.end()) {
            AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                "Material.UnconnectedSurfaceOutput",
                "SurfaceOutput requires one typed Surface connection",
                graph.outputNodeId);
        }

        if (HasCycle(graph, nodeIndices)) {
            AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                "Material.GraphCycle",
                "material v1 graphs must be acyclic");
        }

        return std::none_of(
            outDiagnostics.begin(),
            outDiagnostics.end(),
            [](const MaterialGraphDiagnostic& diagnostic) {
                return diagnostic.severity == MaterialGraphDiagnosticSeverity::Error;
            });
    }

} // namespace HIKARI
