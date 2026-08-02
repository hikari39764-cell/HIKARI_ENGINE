#include "HIKARI_MaterialGraphValidation.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
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
                    { "R", MaterialValueType::Float1 },
                    { "G", MaterialValueType::Float1 },
                    { "B", MaterialValueType::Float1 },
                    { "A", MaterialValueType::Float1 },
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
                    { "Additive", MaterialValueType::Float3 },
                    { "Outline", MaterialValueType::OutlineResponse }
                }, { { "Surface", MaterialValueType::Surface } } };
            case MaterialNodeKind::RampDiffuse:
                return { {
                    { "ShadowThreshold", MaterialValueType::Float1 },
                    { "ShadowSoftness", MaterialValueType::Float1 },
                    { "MidThreshold", MaterialValueType::Float1 },
                    { "ShadowTint", MaterialValueType::Float3 },
                    { "MidTint", MaterialValueType::Float3 },
                    { "LitTint", MaterialValueType::Float3 },
                    { "MaskInfluence", MaterialValueType::Float1 },
                    { "Intensity", MaterialValueType::Float1 },
                    { "DirectGain", MaterialValueType::Float1 },
                    { "Mask", MaterialValueType::Float1 }
                }, { { "Response", MaterialValueType::DiffuseResponse } } };
            case MaterialNodeKind::MultiRampDiffuse:
                return { {
                    { "ShadowThreshold", MaterialValueType::Float1 },
                    { "ShadowSoftness", MaterialValueType::Float1 },
                    { "MidThreshold", MaterialValueType::Float1 },
                    { "ShadowTint", MaterialValueType::Float3 },
                    { "MidTint", MaterialValueType::Float3 },
                    { "LitTint", MaterialValueType::Float3 },
                    { "RampRow", MaterialValueType::Float1 },
                    { "MaskInfluence", MaterialValueType::Float1 },
                    { "RampBlend", MaterialValueType::Float1 },
                    { "Intensity", MaterialValueType::Float1 },
                    { "DirectGain", MaterialValueType::Float1 },
                    { "Mask", MaterialValueType::Float1 }
                }, { { "Response", MaterialValueType::DiffuseResponse } } };
            case MaterialNodeKind::SpecularBand:
                return { {
                    { "Width", MaterialValueType::Float1 },
                    { "Softness", MaterialValueType::Float1 },
                    { "Intensity", MaterialValueType::Float1 },
                    { "Power", MaterialValueType::Float1 },
                    { "Tint", MaterialValueType::Float3 }
                }, { { "Response", MaterialValueType::SpecularResponse } } };
            case MaterialNodeKind::FresnelRim:
                return { {
                    { "Power", MaterialValueType::Float1 },
                    { "Width", MaterialValueType::Float1 },
                    { "Color", MaterialValueType::Float3 },
                    { "Intensity", MaterialValueType::Float1 },
                    { "LightAlignment", MaterialValueType::Float1 }
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
                    { "Width", MaterialValueType::Float1 },
                    { "Mask", MaterialValueType::Float1 }
                }, { { "Response", MaterialValueType::SpecularResponse } } };
            case MaterialNodeKind::FaceSdf:
                return { {
                    { "LightDirection", MaterialValueType::Float3 },
                    { "SDF", MaterialValueType::Float1 },
                    { "Softness", MaterialValueType::Float1 },
                    { "DirectGain", MaterialValueType::Float1 }
                }, { { "Response", MaterialValueType::DiffuseResponse } } };
            case MaterialNodeKind::EyeLighting:
                return { {
                    { "BaseColor", MaterialValueType::Float3 },
                    { "Mask", MaterialValueType::Float1 },
                    { "ViewDirection", MaterialValueType::Float3 }
                }, { { "Color", MaterialValueType::Float3 } } };
            case MaterialNodeKind::SkinLighting:
                return { {
                    { "DiffuseResponse", MaterialValueType::DiffuseResponse },
                    { "Wrap", MaterialValueType::Float1 },
                    { "Subsurface", MaterialValueType::Float1 },
                    { "Tint", MaterialValueType::Float3 },
                    { "Mask", MaterialValueType::Float1 }
                }, { { "Response", MaterialValueType::DiffuseResponse } } };
            case MaterialNodeKind::FabricSheen:
                return { {
                    { "Sheen", MaterialValueType::Float1 },
                    { "FiberRoughness", MaterialValueType::Float1 },
                    { "Power", MaterialValueType::Float1 },
                    { "Intensity", MaterialValueType::Float1 },
                    { "Mask", MaterialValueType::Float1 }
                }, { { "Color", MaterialValueType::Float3 } } };
            case MaterialNodeKind::HybridLighting:
                return { {
                    { "DiffuseResponse", MaterialValueType::DiffuseResponse },
                    { "DirectDiffuseBlend", MaterialValueType::Float1 },
                    { "DirectSpecularBlend", MaterialValueType::Float1 },
                    { "IndirectBlend", MaterialValueType::Float1 }
                }, { { "Response", MaterialValueType::DiffuseResponse } } };
            case MaterialNodeKind::Outline:
                return { {
                    { "Color", MaterialValueType::Float3 },
                    { "Thickness", MaterialValueType::Float1 }
                }, { { "Response", MaterialValueType::OutlineResponse } } };
            case MaterialNodeKind::HairShadow:
                return { {
                    { "DiffuseResponse", MaterialValueType::DiffuseResponse },
                    { "ShadowMask", MaterialValueType::Float1 },
                    { "Strength", MaterialValueType::Float1 }
                }, { { "Response", MaterialValueType::DiffuseResponse } } };
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

        const MaterialGraphNodeData* FindNodeById(
            const MaterialGraphData& graph,
            std::string_view nodeId) noexcept {
            const auto found = std::find_if(
                graph.nodes.begin(), graph.nodes.end(),
                [nodeId](const MaterialGraphNodeData& node) {
                    return node.id == nodeId;
                });
            return found == graph.nodes.end() ? nullptr : &*found;
        }

        const MaterialGraphEdgeData* FindInputEdge(
            const MaterialGraphData& graph,
            std::string_view nodeId,
            std::string_view pin) noexcept {
            const auto found = std::find_if(
                graph.edges.begin(), graph.edges.end(),
                [nodeId, pin](const MaterialGraphEdgeData& edge) {
                    return edge.targetNodeId == nodeId &&
                        edge.targetPin == pin;
                });
            return found == graph.edges.end() ? nullptr : &*found;
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
                node->kind == MaterialNodeKind::Parameter) {
                visiting.erase(std::string(nodeId));
                return true;
            }
            std::vector<std::string_view> requiredInputs{};
            switch (node->kind) {
            case MaterialNodeKind::Add:
            case MaterialNodeKind::Multiply:
            case MaterialNodeKind::Dot:
                requiredInputs = {"A", "B"};
                break;
            case MaterialNodeKind::Lerp:
                requiredInputs = {"A", "B", "Alpha"};
                break;
            case MaterialNodeKind::Clamp:
                requiredInputs = {"Value", "Min", "Max"};
                break;
            case MaterialNodeKind::Saturate:
            case MaterialNodeKind::OneMinus:
                requiredInputs = {"Value"};
                break;
            case MaterialNodeKind::Power:
                requiredInputs = {"Base", "Exponent"};
                break;
            default:
                visiting.erase(std::string(nodeId));
                return false;
            }
            const bool result = std::all_of(
                requiredInputs.begin(), requiredInputs.end(),
                [&](std::string_view pin) {
                    const MaterialGraphEdgeData* edge =
                        FindInputEdge(graph, node->id, pin);
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

        bool IsAdditiveFeature(MaterialNodeKind kind) noexcept {
            return kind == MaterialNodeKind::FresnelRim ||
                kind == MaterialNodeKind::MatCap ||
                kind == MaterialNodeKind::EyeLighting ||
                kind == MaterialNodeKind::FabricSheen;
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
            MaterialNodeKind kind,
            std::string_view pin) noexcept {
            return pin == "Mask" ||
                pin == "ShadowMask" ||
                (kind == MaterialNodeKind::FaceSdf && pin == "SDF");
        }

        std::string_view RuntimeResourceChannel(
            MaterialNodeKind kind,
            std::string_view pin) noexcept {
            if (kind == MaterialNodeKind::FaceSdf && pin == "SDF") {
                return "R";
            }
            if (pin == "ShadowMask" &&
                kind == MaterialNodeKind::HairShadow) {
                return "R";
            }
            if (pin != "Mask") {
                return {};
            }
            switch (kind) {
            case MaterialNodeKind::SkinLighting:
                return "G";
            case MaterialNodeKind::FabricSheen:
                return "B";
            case MaterialNodeKind::MatCap:
                return "A";
            case MaterialNodeKind::RampDiffuse:
            case MaterialNodeKind::MultiRampDiffuse:
            case MaterialNodeKind::AnisotropicHair:
            case MaterialNodeKind::EyeLighting:
                return "R";
            default:
                return {};
            }
        }

        bool IsRuntimeGeneratedInput(
            MaterialNodeKind kind,
            std::string_view pin) noexcept {
            return (kind == MaterialNodeKind::MatCap &&
                    (pin == "Normal" || pin == "ViewDirection")) ||
                (kind == MaterialNodeKind::AnisotropicHair &&
                 pin == "Tangent") ||
                (kind == MaterialNodeKind::FaceSdf &&
                 pin == "LightDirection") ||
                (kind == MaterialNodeKind::EyeLighting &&
                 (pin == "BaseColor" || pin == "ViewDirection"));
        }

        bool IsStylizedNode(MaterialNodeKind kind) noexcept {
            return kind >= MaterialNodeKind::RampDiffuse &&
                kind <= MaterialNodeKind::EyeLighting ||
                kind == MaterialNodeKind::SkinLighting ||
                kind == MaterialNodeKind::FabricSheen ||
                kind == MaterialNodeKind::HybridLighting ||
                kind == MaterialNodeKind::Outline ||
                kind == MaterialNodeKind::HairShadow;
        }

        bool IsCanonicalStylizedOutputEdge(
            MaterialNodeKind source,
            MaterialNodeKind target,
            std::string_view targetPin) noexcept {
            if (source == MaterialNodeKind::RampDiffuse ||
                source == MaterialNodeKind::MultiRampDiffuse ||
                source == MaterialNodeKind::FaceSdf ||
                source == MaterialNodeKind::HairShadow) {
                return targetPin == "DiffuseResponse" &&
                    (target == MaterialNodeKind::PbrSurface ||
                     target == MaterialNodeKind::SkinLighting ||
                     target == MaterialNodeKind::HybridLighting ||
                     target == MaterialNodeKind::HairShadow);
            }
            if (source == MaterialNodeKind::SkinLighting) {
                return targetPin == "DiffuseResponse" &&
                    (target == MaterialNodeKind::PbrSurface ||
                     target == MaterialNodeKind::HybridLighting ||
                     target == MaterialNodeKind::HairShadow);
            }
            if (source == MaterialNodeKind::HybridLighting) {
                return (target == MaterialNodeKind::PbrSurface ||
                        target == MaterialNodeKind::HairShadow) &&
                    targetPin == "DiffuseResponse";
            }
            if (source == MaterialNodeKind::SpecularBand ||
                source == MaterialNodeKind::AnisotropicHair) {
                return target == MaterialNodeKind::PbrSurface &&
                    targetPin == "SpecularResponse";
            }
            if (IsAdditiveFeature(source)) {
                return (target == MaterialNodeKind::PbrSurface &&
                        targetPin == "Additive") ||
                    (target == MaterialNodeKind::Add &&
                     (targetPin == "A" || targetPin == "B"));
            }
            if (source == MaterialNodeKind::Outline) {
                return target == MaterialNodeKind::PbrSurface &&
                    targetPin == "Outline";
            }
            return true;
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
                    if (IsRuntimeGeneratedInput(
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
                    edge.targetPin == "Outline") {
                    continue;
                }
                if (edge.targetPin == "Additive") {
                    if (!IsAdditiveExpression(graph, edge.sourceNodeId)) {
                        AddDiagnostic(
                            diagnostics,
                            MaterialGraphDiagnosticSeverity::Error,
                            "Material.UnsupportedAdditiveExpression",
                            "PBR Additive requires a Fresnel, MatCap, Eye, Fabric, or Add feature stack",
                            edge.id);
                    }
                    continue;
                }
                if (IsStaticNumericExpression(graph, edge.sourceNodeId)) {
                    continue;
                }
                const auto texture = FindTextureExpression(graph, edge);
                if (!texture.has_value() ||
                    !IsExpectedPbrTextureChannel(edge.targetPin, *texture)) {
                    AddDiagnostic(
                        diagnostics,
                        MaterialGraphDiagnosticSeverity::Error,
                        "Material.UnsupportedPbrExpression",
                        "PBR input uses a texture/math expression that the fixed Mesh Shader material path cannot reproduce exactly",
                        edge.id);
                }
            }

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
                        "the runtime uses one packed metallic-roughness texture; connect B to Metallic and G to Roughness from the same sample",
                        node.id);
                }
            }
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
                sourcePinSignature->second,
                targetPinSignature->second,
                IsDynamicNumericInput(targetNode->kind, targetPin))) {
            outReason = "the output and input socket types are incompatible";
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

        ValidateRuntimeSemantics(graph, outDiagnostics);

        return std::none_of(
            outDiagnostics.begin(),
            outDiagnostics.end(),
            [](const MaterialGraphDiagnostic& diagnostic) {
                return diagnostic.severity == MaterialGraphDiagnosticSeverity::Error;
            });
    }

} // namespace HIKARI
