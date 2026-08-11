#include "HIKARI_MaterialGraphValidation.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Assets/Material/Graph/HIKARI_MaterialNodeDefinition.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphQuery.h"

namespace HIKARI {
    namespace {
        constexpr size_t kMaxNodes = 512u;
        constexpr size_t kMaxEdges = 2048u;
        constexpr size_t kMaxParameters = 256u;
        constexpr size_t kMaxResources = 256u;
        struct ResolvedPin final {
            std::string_view id{};
            MaterialValueType type = MaterialValueType::Float1;
            bool acceptsDynamicNumeric = false;
        };
        struct NodeSignature final {
            std::vector<ResolvedPin> inputs{};
            std::vector<ResolvedPin> outputs{};
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
            NodeSignature signature{};
            if (node.kind == MaterialNodeKind::FunctionCall &&
                (!node.functionInputs.empty() ||
                 !node.functionOutputs.empty())) {
                signature.inputs.reserve(node.functionInputs.size());
                for (const MaterialGraphParameterData& pin :
                     node.functionInputs) {
                    signature.inputs.push_back({
                        pin.id, pin.defaultValue.type, false});
                }
                signature.outputs.reserve(node.functionOutputs.size());
                for (const MaterialGraphParameterData& pin :
                     node.functionOutputs) {
                    signature.outputs.push_back({
                        pin.id, pin.defaultValue.type, false});
                }
                return signature;
            }
            const MaterialNodeDefinition* definition =
                FindMaterialNodeDefinition(node.kind);
            if (definition == nullptr) {
                return signature;
            }
            std::optional<MaterialValueType> parameterType{};
            if (const auto parameter = parameters.find(node.referenceId);
                parameter != parameters.end()) {
                parameterType = parameter->second;
            }
            const auto appendPins = [&](
                std::span<const MaterialNodePinDefinition> definitions,
                std::vector<ResolvedPin>& pins) {
                pins.reserve(definitions.size());
                for (const MaterialNodePinDefinition& pin : definitions) {
                    pins.push_back(ResolvedPin{
                        pin.id,
                        ResolveMaterialNodePinType(pin, node, parameterType),
                        pin.acceptsDynamicNumeric,
                    });
                }
            };
            appendPins(definition->inputs, signature.inputs);
            appendPins(definition->outputs, signature.outputs);
            return signature;
        }

        const ResolvedPin* FindPin(
            const std::vector<ResolvedPin>& pins,
            std::string_view name) {
            const auto result = std::find_if(
                pins.begin(),
                pins.end(),
                [name](const ResolvedPin& pin) { return pin.id == name; });
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

        const MaterialGraphNodeData* FindNodeById(
            const MaterialGraphData& graph,
            std::string_view nodeId) noexcept {
            return FindMaterialGraphNode(graph, nodeId);
        }

        const MaterialGraphEdgeData* FindInputEdge(
            const MaterialGraphData& graph,
            std::string_view nodeId,
            std::string_view pin) noexcept {
            return FindMaterialGraphInputEdge(graph, nodeId, pin);
        }

        bool IsStaticNumericExpression(
            const MaterialGraphData& graph,
            std::string_view nodeId,
            std::unordered_set<std::string>& visiting) {
            if (!visiting.emplace(nodeId).second) {
                return false;
            }
            const MaterialGraphNodeData* node = FindNodeById(graph, nodeId);
            if (node == nullptr) {
                visiting.erase(std::string(nodeId));
                return false;
            }
            if (node->kind == MaterialNodeKind::Constant ||
                node->kind == MaterialNodeKind::Parameter ||
                node->kind == MaterialNodeKind::Variable) {
                visiting.erase(std::string(nodeId));
                return true;
            }
            const MaterialNodeDefinition* definition =
                FindMaterialNodeDefinition(node->kind);
            if (definition == nullptr || !definition->staticallyEvaluable) {
                visiting.erase(std::string(nodeId));
                return false;
            }
            const bool result = std::all_of(
                definition->inputs.begin(), definition->inputs.end(),
                [&](const MaterialNodePinDefinition& pin) {
                    const MaterialGraphEdgeData* edge =
                        FindInputEdge(graph, node->id, pin.id);
                    return edge != nullptr && IsStaticNumericExpression(
                        graph, edge->sourceNodeId, visiting);
                });
            visiting.erase(std::string(nodeId));
            return result;
        }

        bool IsStaticNumericExpression(
            const MaterialGraphData& graph,
            std::string_view nodeId) {
            std::unordered_set<std::string> visiting{};
            return IsStaticNumericExpression(graph, nodeId, visiting);
        }

        struct TextureExpression final {
            const MaterialGraphResourceData* resource = nullptr;
            std::string_view sampledPin{};
            bool normalUnpacked = false;
        };

        std::optional<TextureExpression> FindTextureExpression(
            const MaterialGraphData& graph,
            std::string_view nodeId,
            std::string_view outputPin,
            std::unordered_set<std::string>& visiting) {
            if (!visiting.emplace(nodeId).second) {
                return std::nullopt;
            }
            const MaterialGraphNodeData* node = FindNodeById(graph, nodeId);
            if (node == nullptr) {
                visiting.erase(std::string(nodeId));
                return std::nullopt;
            }
            if (node->kind == MaterialNodeKind::TextureSample) {
                if (FindInputEdge(graph, node->id, "UV") != nullptr) {
                    visiting.erase(std::string(nodeId));
                    return std::nullopt;
                }
                const auto resource = std::find_if(
                    graph.resources.begin(), graph.resources.end(),
                    [&](const MaterialGraphResourceData& value) {
                        return value.id == node->referenceId;
                    });
                visiting.erase(std::string(nodeId));
                return resource == graph.resources.end()
                    ? std::nullopt
                    : std::optional<TextureExpression>{TextureExpression{
                        &*resource, outputPin, false}};
            }
            if (node->kind == MaterialNodeKind::NormalUnpack) {
                const MaterialGraphEdgeData* edge =
                    FindInputEdge(graph, node->id, "PackedNormal");
                auto result = edge == nullptr
                    ? std::optional<TextureExpression>{}
                    : FindTextureExpression(
                        graph, edge->sourceNodeId, edge->sourcePin, visiting);
                if (result.has_value()) {
                    result->normalUnpacked = true;
                }
                visiting.erase(std::string(nodeId));
                return result;
            }
            if (node->kind == MaterialNodeKind::Multiply) {
                const MaterialGraphEdgeData* a =
                    FindInputEdge(graph, node->id, "A");
                const MaterialGraphEdgeData* b =
                    FindInputEdge(graph, node->id, "B");
                if (a != nullptr && b != nullptr) {
                    auto texture = FindTextureExpression(
                        graph, a->sourceNodeId, a->sourcePin, visiting);
                    if (texture.has_value() && IsStaticNumericExpression(
                            graph, b->sourceNodeId)) {
                        visiting.erase(std::string(nodeId));
                        return texture;
                    }
                    texture = FindTextureExpression(
                        graph, b->sourceNodeId, b->sourcePin, visiting);
                    if (texture.has_value() && IsStaticNumericExpression(
                            graph, a->sourceNodeId)) {
                        visiting.erase(std::string(nodeId));
                        return texture;
                    }
                }
            }
            visiting.erase(std::string(nodeId));
            return std::nullopt;
        }

        std::optional<TextureExpression> FindTextureExpression(
            const MaterialGraphData& graph,
            const MaterialGraphEdgeData& edge) {
            std::unordered_set<std::string> visiting{};
            return FindTextureExpression(
                graph, edge.sourceNodeId, edge.sourcePin, visiting);
        }

        bool IsAdditiveFeature(const MaterialNodeTypeId& typeId) noexcept {
            const MaterialNodeDefinition* definition =
                FindMaterialNodeDefinition(typeId);
            return definition != nullptr &&
                definition->outputRoute == MaterialNodeOutputRoute::Additive;
        }

        bool IsAdditiveExpression(
            const MaterialGraphData& graph,
            std::string_view nodeId,
            std::unordered_set<std::string>& visiting) {
            if (!visiting.emplace(nodeId).second) {
                return false;
            }
            const MaterialGraphNodeData* node = FindNodeById(graph, nodeId);
            if (node == nullptr) {
                visiting.erase(std::string(nodeId));
                return false;
            }
            if (IsAdditiveFeature(node->kind)) {
                visiting.erase(std::string(nodeId));
                return true;
            }
            if (node->kind != MaterialNodeKind::Add) {
                visiting.erase(std::string(nodeId));
                return false;
            }
            bool valid = true;
            for (const std::string_view pin : {std::string_view{"A"},
                                               std::string_view{"B"}}) {
                const MaterialGraphEdgeData* edge =
                    FindInputEdge(graph, node->id, pin);
                if (edge == nullptr) {
                    continue;
                }
                valid = valid && IsAdditiveExpression(
                    graph, edge->sourceNodeId, visiting);
            }
            visiting.erase(std::string(nodeId));
            // Additive feature stacks use zero as the neutral value for a
            // missing socket. This keeps node/edge deletion executable
            // instead of silently falling back to the previous program.
            return valid;
        }

        bool IsAdditiveExpression(
            const MaterialGraphData& graph,
            std::string_view nodeId) {
            std::unordered_set<std::string> visiting{};
            return IsAdditiveExpression(graph, nodeId, visiting);
        }

        bool IsRuntimeResourceInput(
            const MaterialNodeTypeId& typeId,
            std::string_view pin) noexcept {
            const MaterialNodePinDefinition* definition =
                FindMaterialNodeInputDefinition(typeId, pin);
            return definition != nullptr &&
                !definition->runtimeResourceChannel.empty();
        }

        std::string_view RuntimeResourceChannel(
            const MaterialNodeTypeId& typeId,
            std::string_view pin) noexcept {
            const MaterialNodePinDefinition* definition =
                FindMaterialNodeInputDefinition(typeId, pin);
            return definition == nullptr
                ? std::string_view{}
                : definition->runtimeResourceChannel;
        }

        bool IsStylizedNode(const MaterialNodeTypeId& typeId) noexcept {
            const MaterialNodeDefinition* definition =
                FindMaterialNodeDefinition(typeId);
            return definition != nullptr &&
                definition->semanticClass == MaterialNodeSemanticClass::Shading;
        }

        bool IsCanonicalStylizedOutputEdge(
            const MaterialNodeTypeId& source,
            const MaterialNodeTypeId& target,
            std::string_view targetPin) noexcept {
            const MaterialNodeDefinition* definition =
                FindMaterialNodeDefinition(source);
            if (definition == nullptr) {
                return false;
            }
            switch (definition->outputRoute) {
            case MaterialNodeOutputRoute::DiffuseBase:
                return targetPin == "DiffuseResponse" &&
                    (target == MaterialNodeKind::PbrSurface ||
                     target == MaterialNodeKind::SkinLighting ||
                     target == MaterialNodeKind::HybridLighting ||
                     target == MaterialNodeKind::HairShadow);
            case MaterialNodeOutputRoute::DiffuseAfterSkin:
                return targetPin == "DiffuseResponse" &&
                    (target == MaterialNodeKind::PbrSurface ||
                     target == MaterialNodeKind::HybridLighting ||
                     target == MaterialNodeKind::HairShadow);
            case MaterialNodeOutputRoute::DiffuseFinal:
                return (target == MaterialNodeKind::PbrSurface ||
                        target == MaterialNodeKind::HairShadow) &&
                    targetPin == "DiffuseResponse";
            case MaterialNodeOutputRoute::Specular:
                return target == MaterialNodeKind::PbrSurface &&
                    targetPin == "SpecularResponse";
            case MaterialNodeOutputRoute::Eye:
                return target == MaterialNodeKind::PbrSurface &&
                    targetPin == "Eye";
            case MaterialNodeOutputRoute::Additive:
                return (target == MaterialNodeKind::PbrSurface &&
                        targetPin == "Additive") ||
                    (target == MaterialNodeKind::Add &&
                     (targetPin == "A" || targetPin == "B"));
            case MaterialNodeOutputRoute::Outline:
                return target == MaterialNodeKind::PbrSurface &&
                    targetPin == "Outline";
            case MaterialNodeOutputRoute::Unrestricted:
            default:
                return true;
            }
        }

        bool IsExpectedPbrTextureChannel(
            std::string_view targetPin,
            const TextureExpression& texture) noexcept {
            if (targetPin == "BaseColor") return texture.sampledPin == "Color";
            if (targetPin == "Normal") {
                return texture.normalUnpacked &&
                    texture.sampledPin == "RGB";
            }
            if (targetPin == "Metallic") return texture.sampledPin == "B";
            if (targetPin == "Roughness") return texture.sampledPin == "G";
            if (targetPin == "Occlusion") return texture.sampledPin == "R";
            if (targetPin == "Emissive") return texture.sampledPin == "RGB";
            if (targetPin == "Specular") return texture.sampledPin == "A";
            if (targetPin == "SpecularColor") return texture.sampledPin == "RGB";
            return false;
        }

        std::unordered_set<std::string> CollectRuntimeReachableNodes(
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

        void ValidateRuntimeSemantics(
            const MaterialGraphData& graph,
            std::vector<MaterialGraphDiagnostic>& diagnostics) {
            const std::unordered_set<std::string> reachable =
                CollectRuntimeReachableNodes(graph);
            for (const MaterialGraphEdgeData& edge : graph.edges) {
                if (!reachable.contains(edge.targetNodeId)) {
                    continue;
                }
                const MaterialGraphNodeData* source =
                    FindNodeById(graph, edge.sourceNodeId);
                const MaterialGraphNodeData* target =
                    FindNodeById(graph, edge.targetNodeId);
                if (source == nullptr || target == nullptr) {
                    continue;
                }
                if (IsStylizedNode(source->kind) &&
                    !IsCanonicalStylizedOutputEdge(
                        source->kind, target->kind, edge.targetPin)) {
                    AddDiagnostic(
                        diagnostics, MaterialGraphDiagnosticSeverity::Error,
                        "Material.UnsupportedResponseRoute",
                        "stylized response output is connected to a runtime channel that does not execute its meaning",
                        edge.id);
                }
                if (source->kind == MaterialNodeKind::Add &&
                    IsAdditiveExpression(graph, source->id) &&
                    !((target->kind == MaterialNodeKind::PbrSurface &&
                       edge.targetPin == "Additive") ||
                      (target->kind == MaterialNodeKind::Add &&
                       (edge.targetPin == "A" || edge.targetPin == "B")))) {
                    AddDiagnostic(
                        diagnostics, MaterialGraphDiagnosticSeverity::Error,
                        "Material.UnsupportedAdditiveRoute",
                        "runtime additive feature stacks may only feed Add nodes or PBR Additive",
                        edge.id);
                }

                if (IsStylizedNode(target->kind)) {
                    if (edge.targetPin == "DiffuseResponse") {
                        continue;
                    }
                    if (IsMaterialGraphRuntimeGeneratedInput(
                            target->kind, edge.targetPin)) {
                        AddDiagnostic(
                            diagnostics,
                            MaterialGraphDiagnosticSeverity::Error,
                            "Material.RuntimeDrivenInput",
                            "this socket is supplied by the runtime shading context and cannot be authored by a graph edge",
                            edge.id);
                        continue;
                    }
                    if (IsRuntimeResourceInput(
                            target->kind, edge.targetPin)) {
                        const auto texture = FindTextureExpression(graph, edge);
                        const std::string_view expectedChannel =
                            RuntimeResourceChannel(
                                target->kind, edge.targetPin);
                        if (!texture.has_value() ||
                            expectedChannel.empty() ||
                            texture->sampledPin != expectedChannel) {
                            AddDiagnostic(
                                diagnostics,
                                MaterialGraphDiagnosticSeverity::Error,
                                "Material.UnsupportedMaskExpression",
                                "runtime mask and SDF sockets require the feature's documented packed texture channel",
                                edge.id);
                        }
                        continue;
                    }
                    if (!IsStaticNumericExpression(
                            graph, edge.sourceNodeId)) {
                        AddDiagnostic(
                            diagnostics,
                            MaterialGraphDiagnosticSeverity::Error,
                            "Material.DynamicFeatureParameter",
                            "stylized node controls require a parameter, constant, or statically evaluable math chain",
                            edge.id);
                    }
                }

                if (target->kind != MaterialNodeKind::PbrSurface) {
                    continue;
                }
                // Material Functions have their own compiler/runtime support
                // diagnostic. Do not mask that actionable error with the
                // fixed-surface expression diagnostic below.
                if (source->kind == MaterialNodeKind::FunctionCall) {
                    continue;
                }
                if (edge.targetPin == "DiffuseResponse" ||
                    edge.targetPin == "SpecularResponse" ||
                    edge.targetPin == "Eye" ||
                    edge.targetPin == "Outline") {
                    continue;
                }
                if (edge.targetPin == "Additive") {
                    if (!IsAdditiveExpression(graph, edge.sourceNodeId)) {
                        AddDiagnostic(
                            diagnostics,
                            MaterialGraphDiagnosticSeverity::Error,
                            "Material.UnsupportedAdditiveExpression",
                            "PBR Additive requires a Fresnel, MatCap, Fabric, or Add feature stack; connect Eye Layer to the dedicated Eye socket",
                            edge.id);
                    }
                    continue;
                }
                // Numeric PBR sockets are evaluated by the bounded pixel
                // expression program. Type compatibility and acyclic graph
                // validation above are the authoring contract; the Runtime
                // registry enforces instruction/register/resource budgets.
            }

            // The compatibility projection still stores metallic and
            // roughness in one packed texture. General pixel expressions can
            // author either socket independently; only the legacy direct
            // texture fast path retains the paired-channel requirement.
            for (const MaterialGraphNodeData& node : graph.nodes) {
                if (node.kind != MaterialNodeKind::PbrSurface ||
                    !reachable.contains(node.id)) {
                    continue;
                }
                const MaterialGraphEdgeData* metallic =
                    FindInputEdge(graph, node.id, "Metallic");
                const MaterialGraphEdgeData* roughness =
                    FindInputEdge(graph, node.id, "Roughness");
                const auto metallicTexture = metallic == nullptr
                    ? std::optional<TextureExpression>{}
                    : FindTextureExpression(graph, *metallic);
                const auto roughnessTexture = roughness == nullptr
                    ? std::optional<TextureExpression>{}
                    : FindTextureExpression(graph, *roughness);
                if (metallicTexture.has_value() !=
                        roughnessTexture.has_value() ||
                    (metallicTexture.has_value() &&
                     metallicTexture->resource != roughnessTexture->resource)) {
                    AddDiagnostic(
                        diagnostics,
                        MaterialGraphDiagnosticSeverity::Error,
                        "Material.MetallicRoughnessPairRequired",
                        "the legacy packed metallic-roughness path requires B and G from the same sample; connect UV or use expression math for independent channels",
                        node.id);
                }
            }
        }

    } // namespace

    std::vector<std::string_view> GetMaterialGraphNodeInputPins(
        MaterialNodeKind kind) {
        return GetMaterialGraphNodeInputPins(MaterialNodeTypeId{ kind });
    }

    std::vector<std::string_view> GetMaterialGraphNodeInputPins(
        const MaterialNodeTypeId& typeId) {
        MaterialGraphNodeData node{};
        node.kind = typeId;
        node.valueType = MaterialValueType::Float1;
        node.value.type = MaterialValueType::Float1;
        return GetMaterialGraphNodeInputPins(node);
    }

    std::vector<std::string_view> GetMaterialGraphNodeInputPins(
        const MaterialGraphNodeData& node) {
        const NodeSignature signature = MakeSignature(node, {});
        std::vector<std::string_view> pins{};
        pins.reserve(signature.inputs.size());
        std::transform(
            signature.inputs.begin(), signature.inputs.end(),
            std::back_inserter(pins),
            [](const ResolvedPin& pin) { return pin.id; });
        return pins;
    }

    std::vector<std::string_view> GetMaterialGraphNodeOutputPins(
        MaterialNodeKind kind) {
        return GetMaterialGraphNodeOutputPins(MaterialNodeTypeId{ kind });
    }

    std::vector<std::string_view> GetMaterialGraphNodeOutputPins(
        const MaterialNodeTypeId& typeId) {
        MaterialGraphNodeData node{};
        node.kind = typeId;
        node.valueType = MaterialValueType::Float1;
        node.value.type = MaterialValueType::Float1;
        return GetMaterialGraphNodeOutputPins(node);
    }

    std::vector<std::string_view> GetMaterialGraphNodeOutputPins(
        const MaterialGraphNodeData& node) {
        const NodeSignature signature = MakeSignature(node, {});
        std::vector<std::string_view> pins{};
        pins.reserve(signature.outputs.size());
        std::transform(
            signature.outputs.begin(), signature.outputs.end(),
            std::back_inserter(pins),
            [](const ResolvedPin& pin) { return pin.id; });
        return pins;
    }

    bool IsMaterialGraphRuntimeGeneratedInput(
        MaterialNodeKind kind,
        std::string_view pin) noexcept {
        return IsMaterialGraphRuntimeGeneratedInput(
            MaterialNodeTypeId{ kind }, pin);
    }

    bool IsMaterialGraphRuntimeGeneratedInput(
        const MaterialNodeTypeId& typeId,
        std::string_view pin) noexcept {
        const MaterialNodePinDefinition* definition =
            FindMaterialNodeInputDefinition(typeId, pin);
        return definition != nullptr && definition->runtimeGenerated;
    }

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

    bool ReconcileMaterialGraphDynamicNodeTypes(
        MaterialGraphData& graph) {
        std::unordered_map<std::string, MaterialValueType> parameters{};
        for (const MaterialGraphParameterData& parameter : graph.parameters) {
            parameters.emplace(parameter.id, parameter.defaultValue.type);
        }
        const auto findNode = [&](std::string_view id)
            -> const MaterialGraphNodeData* {
            const auto found = std::find_if(
                graph.nodes.begin(), graph.nodes.end(),
                [id](const MaterialGraphNodeData& node) {
                    return node.id == id;
                });
            return found == graph.nodes.end() ? nullptr : &*found;
        };
        bool anyChanged = false;
        bool passChanged = false;
        size_t pass = 0u;
        do {
            passChanged = false;
            for (MaterialGraphNodeData& node : graph.nodes) {
                const MaterialNodeDefinition* definition =
                    FindMaterialNodeDefinition(node.kind);
                if (definition == nullptr ||
                    std::none_of(
                        definition->inputs.begin(),
                        definition->inputs.end(),
                        [](const MaterialNodePinDefinition& pin) {
                            return pin.typeSource ==
                                MaterialNodePinTypeSource::NodeValue;
                        })) {
                    continue;
                }
                uint32_t widestComponents = 0u;
                MaterialValueType widestType = node.valueType;
                for (const MaterialGraphEdgeData& edge : graph.edges) {
                    if (edge.targetNodeId != node.id) {
                        continue;
                    }
                    const MaterialNodePinDefinition* inputDefinition =
                        FindMaterialNodeInputDefinition(
                            node.kind, edge.targetPin);
                    if (inputDefinition == nullptr ||
                        inputDefinition->typeSource !=
                            MaterialNodePinTypeSource::NodeValue) {
                        continue;
                    }
                    const MaterialGraphNodeData* source =
                        findNode(edge.sourceNodeId);
                    if (source == nullptr) {
                        continue;
                    }
                    const NodeSignature sourceSignature =
                        MakeSignature(*source, parameters);
                    const ResolvedPin* output =
                        FindPin(sourceSignature.outputs, edge.sourcePin);
                    if (output == nullptr ||
                        !IsMaterialNumericType(output->type)) {
                        continue;
                    }
                    const uint32_t components =
                        GetMaterialValueComponentCount(output->type);
                    if (components > widestComponents) {
                        widestComponents = components;
                        widestType = output->type;
                    }
                }
                if (widestComponents != 0u &&
                    node.valueType != widestType) {
                    node.valueType = widestType;
                    node.value.type = widestType;
                    passChanged = true;
                    anyChanged = true;
                }
            }
            ++pass;
        } while (passChanged && pass < graph.nodes.size());
        return anyChanged;
    }

    bool CanConnectMaterialGraphPins(
        const MaterialGraphData& graph,
        std::string_view sourceNodeId,
        std::string_view sourcePin,
        std::string_view targetNodeId,
        std::string_view targetPin,
        std::string& outReason) {
        outReason.clear();
        if (sourceNodeId == targetNodeId) {
            outReason = "a node cannot connect to itself";
            return false;
        }

        std::unordered_map<std::string, MaterialValueType> parameters{};
        for (const MaterialGraphParameterData& parameter : graph.parameters) {
            parameters[parameter.id] = parameter.defaultValue.type;
        }
        const auto findNode = [&](std::string_view id)
            -> const MaterialGraphNodeData* {
            const auto found = std::find_if(
                graph.nodes.begin(), graph.nodes.end(),
                [id](const MaterialGraphNodeData& node) {
                    return node.id == id;
                });
            return found == graph.nodes.end() ? nullptr : &*found;
        };
        const MaterialGraphNodeData* sourceNode = findNode(sourceNodeId);
        const MaterialGraphNodeData* targetNode = findNode(targetNodeId);
        if (sourceNode == nullptr || targetNode == nullptr) {
            outReason = "the connection references a missing node";
            return false;
        }

        const NodeSignature sourceSignature =
            MakeSignature(*sourceNode, parameters);
        const NodeSignature targetSignature =
            MakeSignature(*targetNode, parameters);
        const auto* sourcePinSignature =
            FindPin(sourceSignature.outputs, sourcePin);
        const auto* targetPinSignature =
            FindPin(targetSignature.inputs, targetPin);
        if (sourcePinSignature == nullptr || targetPinSignature == nullptr) {
            outReason = "the selected socket is not part of this node type";
            return false;
        }
        if (!ArePinTypesCompatible(
                sourcePinSignature->type,
                targetPinSignature->type,
                targetPinSignature->acceptsDynamicNumeric)) {
            outReason =
                std::string(sourceNodeId) + "." +
                std::string(sourcePin) + " (" +
                std::string(ToString(sourcePinSignature->type)) +
                ") cannot connect to " +
                std::string(targetNodeId) + "." +
                std::string(targetPin) + " (" +
                std::string(ToString(targetPinSignature->type)) + ")";
            return false;
        }

        MaterialGraphData candidate = graph;
        std::erase_if(candidate.edges,
            [&](const MaterialGraphEdgeData& edge) {
                return edge.targetNodeId == targetNodeId &&
                    edge.targetPin == targetPin;
            });
        MaterialGraphEdgeData edge{};
        edge.id = "EditorConnectionProbe";
        edge.sourceNodeId = std::string(sourceNodeId);
        edge.sourcePin = std::string(sourcePin);
        edge.targetNodeId = std::string(targetNodeId);
        edge.targetPin = std::string(targetPin);
        candidate.edges.push_back(std::move(edge));

        std::unordered_map<std::string, size_t> nodeIndices{};
        for (size_t index = 0u; index < candidate.nodes.size(); ++index) {
            nodeIndices.emplace(candidate.nodes[index].id, index);
        }
        if (HasCycle(candidate, nodeIndices)) {
            outReason = "the connection would create a graph cycle";
            return false;
        }
        std::vector<MaterialGraphDiagnostic> runtimeDiagnostics{};
        ValidateRuntimeSemantics(candidate, runtimeDiagnostics);
        const auto unsupported = std::find_if(
            runtimeDiagnostics.begin(), runtimeDiagnostics.end(),
            [](const MaterialGraphDiagnostic& diagnostic) {
                return diagnostic.severity ==
                           MaterialGraphDiagnosticSeverity::Error &&
                    diagnostic.subjectId == "EditorConnectionProbe";
            });
        if (unsupported != runtimeDiagnostics.end()) {
            outReason = unsupported->message;
            return false;
        }
        return true;
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
        std::unordered_map<std::string, MaterialValueType>
            runtimeVariableTypes{};
        const std::unordered_set<std::string> runtimeReachable =
            CollectRuntimeReachableNodes(graph);
        for (size_t index = 0; index < graph.nodes.size(); ++index) {
            const MaterialGraphNodeData& node = graph.nodes[index];
            if (!IsValidMaterialStableId(node.id) ||
                !nodeIndices.emplace(node.id, index).second) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.InvalidNodeId",
                    "material node IDs must be stable and unique", node.id);
            }
            const MaterialNodeDefinition* definition =
                FindMaterialNodeDefinition(node.kind);
            if (definition == nullptr) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.UnknownNodeType",
                    "material node TypeId is not registered by this compiler",
                    node.kind.value);
            } else if (options.requireDomainOutput &&
                !IsMaterialNodeAvailableForDomain(
                    node.kind, options.domain)) {
                    AddDiagnostic(
                        outDiagnostics,
                        MaterialGraphDiagnosticSeverity::Error,
                        "Material.NodeDomainMismatch",
                        options.domain == MaterialDomain::Particle
                            ? "particle material graphs accept numeric expressions and ParticleOutput, not Surface shading nodes"
                            : "surface material graphs cannot contain ParticleOutput",
                        node.id);
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
            if (node.kind == MaterialNodeKind::Variable) {
                if (!IsValidMaterialStableId(node.referenceId)) {
                    AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                        "Material.MissingRuntimeVariable",
                        "variable nodes require one stable runtime variable ID",
                        node.id);
                }
                if ((node.valueType != MaterialValueType::Bool &&
                        !IsMaterialNumericType(node.valueType)) ||
                    node.value.type != node.valueType) {
                    AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                        "Material.InvalidRuntimeVariableType",
                        "variable node type and fallback must be the same bool or numeric type",
                        node.id);
                }
                if (runtimeReachable.contains(node.id) &&
                    IsValidMaterialStableId(node.referenceId) &&
                    (node.valueType == MaterialValueType::Bool ||
                     IsMaterialNumericType(node.valueType))) {
                    const auto [type, inserted] =
                        runtimeVariableTypes.emplace(
                            node.referenceId,
                            node.valueType);
                    if (!inserted && type->second != node.valueType) {
                        AddDiagnostic(
                            outDiagnostics,
                            MaterialGraphDiagnosticSeverity::Error,
                            "Material.RuntimeVariableTypeConflict",
                            "all Variable nodes using one runtime ID must use the same material value type",
                            node.referenceId);
                    }
                }
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

        if (options.requireDomainOutput) {
            const auto output = nodeIndices.find(graph.outputNodeId);
            const MaterialNodeKind expectedOutput =
                options.domain == MaterialDomain::Particle
                    ? MaterialNodeKind::ParticleOutput
                    : MaterialNodeKind::SurfaceOutput;
            if (output == nodeIndices.end() ||
                graph.nodes[output->second].kind != expectedOutput) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    options.domain == MaterialDomain::Particle
                        ? "Material.MissingParticleOutput"
                        : "Material.MissingSurfaceOutput",
                    options.domain == MaterialDomain::Particle
                        ? "particle material graph requires an explicit ParticleOutput node"
                        : "surface material graph requires an explicit SurfaceOutput node",
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
                sourcePin->type,
                targetPin->type,
                targetPin->acceptsDynamicNumeric)) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.PinTypeMismatch",
                    "material edge connects incompatible typed pins", edge.id);
            }
        }

        if (options.requireDomainOutput && !graph.outputNodeId.empty()) {
            if (options.domain == MaterialDomain::Surface &&
                targetPins.find(graph.outputNodeId + ":Surface") == targetPins.end()) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.UnconnectedSurfaceOutput",
                    "SurfaceOutput requires one typed Surface connection",
                    graph.outputNodeId);
            } else if (options.domain == MaterialDomain::Particle &&
                targetPins.find(graph.outputNodeId + ":Color") == targetPins.end()) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.UnconnectedParticleOutput",
                    "ParticleOutput requires one typed Color connection",
                    graph.outputNodeId);
            }
        }

        if (HasCycle(graph, nodeIndices)) {
            AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                "Material.GraphCycle",
                "material v1 graphs must be acyclic");
        }

        std::unordered_set<std::string> groupIds{};
        for (const MaterialGraphGroupData& group : graph.groups) {
            if (!IsValidMaterialStableId(group.id) ||
                !groupIds.emplace(group.id).second) {
                AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Error,
                    "Material.InvalidGroupId",
                    "material group IDs must be stable and unique", group.id);
            }
            for (const std::string& nodeId : group.nodeIds) {
                if (nodeIndices.find(nodeId) == nodeIndices.end()) {
                    AddDiagnostic(outDiagnostics, MaterialGraphDiagnosticSeverity::Warning,
                        "Material.InvalidGroupMember",
                        "material group references a missing node", group.id);
                }
            }
        }

        ValidateRuntimeSemantics(graph, outDiagnostics);

        return std::none_of(
            outDiagnostics.begin(),
            outDiagnostics.end(),
            [](const MaterialGraphDiagnostic& diagnostic) {
                return diagnostic.severity == MaterialGraphDiagnosticSeverity::Error;
            });
    }

} // namespace HIKARI
