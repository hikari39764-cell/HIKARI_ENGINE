#include "HIKARI_MaterialAssetData.h"

#include "Assets/Material/Graph/HIKARI_MaterialGraphQuery.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace HIKARI {

    namespace {

        const MaterialGraphParameterData* FindParameter(
            const MaterialGraphData& graph,
            std::string_view id) noexcept {
            return FindMaterialGraphParameter(graph, id);
        }

        const MaterialGraphResourceData* FindResource(
            const MaterialGraphData& graph,
            MaterialResourceSemantic semantic) noexcept {
            const auto found = std::find_if(
                graph.resources.begin(), graph.resources.end(),
                [semantic](const MaterialGraphResourceData& resource) {
                    return resource.semantic == semantic;
                });
            return found == graph.resources.end() ? nullptr : &*found;
        }

        const MaterialGraphResourceData* FindResource(
            const MaterialGraphData& graph,
            std::string_view id) noexcept {
            return FindMaterialGraphResource(graph, id);
        }

        const MaterialGraphNodeData* FindNode(
            const MaterialGraphData& graph,
            std::string_view id) noexcept {
            return FindMaterialGraphNode(graph, id);
        }

        const MaterialGraphEdgeData* FindInputEdge(
            const MaterialGraphData& graph,
            std::string_view nodeId,
            std::string_view inputPin) noexcept {
            return FindMaterialGraphInputEdge(graph, nodeId, inputPin);
        }

        float ReadComponent(
            const MaterialValueData& value,
            uint32_t component) noexcept {
            const uint32_t count = GetMaterialValueComponentCount(value.type);
            const float components[]{
                value.numeric.x, value.numeric.y,
                value.numeric.z, value.numeric.w
            };
            return components[count <= 1u
                ? 0u
                : (std::min)(component, count - 1u)];
        }

        std::optional<MaterialValueData> EvaluateNumericOutput(
            const MaterialGraphData& graph,
            std::string_view nodeId,
            std::string_view outputPin,
            std::unordered_map<std::string, MaterialValueData>& memo,
            std::unordered_set<std::string>& evaluating) {
            const std::string key = std::string(nodeId) + ":" +
                std::string(outputPin);
            if (const auto cached = memo.find(key); cached != memo.end()) {
                return cached->second;
            }
            if (!evaluating.emplace(key).second) {
                return std::nullopt;
            }

            const MaterialGraphNodeData* node = FindNode(graph, nodeId);
            if (node == nullptr) {
                evaluating.erase(key);
                return std::nullopt;
            }
            auto input = [&](std::string_view pin)
                -> std::optional<MaterialValueData> {
                const MaterialGraphEdgeData* edge =
                    FindInputEdge(graph, node->id, pin);
                return edge == nullptr
                    ? std::nullopt
                    : EvaluateNumericOutput(graph, edge->sourceNodeId,
                        edge->sourcePin, memo, evaluating);
            };

            std::optional<MaterialValueData> result{};
            if (node->kind == MaterialNodeKind::Constant) {
                result = node->value;
            } else if (node->kind == MaterialNodeKind::Parameter) {
                if (const MaterialGraphParameterData* parameter =
                        FindParameter(graph, node->referenceId)) {
                    result = parameter->defaultValue;
                }
            } else if (node->kind == MaterialNodeKind::Add ||
                node->kind == MaterialNodeKind::Multiply ||
                node->kind == MaterialNodeKind::Lerp ||
                node->kind == MaterialNodeKind::Clamp ||
                node->kind == MaterialNodeKind::Saturate ||
                node->kind == MaterialNodeKind::OneMinus ||
                node->kind == MaterialNodeKind::Power ||
                node->kind == MaterialNodeKind::Dot) {
                const auto a = input(
                    node->kind == MaterialNodeKind::Clamp ||
                    node->kind == MaterialNodeKind::Saturate ||
                    node->kind == MaterialNodeKind::OneMinus
                        ? "Value"
                    : node->kind == MaterialNodeKind::Power ? "Base" : "A");
                const auto b =
                    node->kind == MaterialNodeKind::Saturate ||
                    node->kind == MaterialNodeKind::OneMinus
                        ? std::optional<MaterialValueData>{}
                        : input(node->kind == MaterialNodeKind::Power
                            ? "Exponent"
                            : node->kind == MaterialNodeKind::Clamp
                                ? "Min" : "B");
                const auto c = node->kind == MaterialNodeKind::Lerp
                        ? input("Alpha")
                    : node->kind == MaterialNodeKind::Clamp
                        ? input("Max")
                        : std::optional<MaterialValueData>{};
                const bool hasRequired = a.has_value() &&
                    (node->kind == MaterialNodeKind::Saturate ||
                     node->kind == MaterialNodeKind::OneMinus || b.has_value()) &&
                    (node->kind != MaterialNodeKind::Lerp || c.has_value()) &&
                    (node->kind != MaterialNodeKind::Clamp || c.has_value());
                if (hasRequired) {
                    MaterialValueData value{};
                    value.type = node->kind == MaterialNodeKind::Dot
                        ? MaterialValueType::Float1 : node->valueType;
                    float* components = &value.numeric.x;
                    const uint32_t componentCount =
                        node->kind == MaterialNodeKind::Dot
                            ? 1u
                            : GetMaterialValueComponentCount(node->valueType);
                    if (node->kind == MaterialNodeKind::Dot) {
                        components[0] =
                            ReadComponent(*a, 0u) * ReadComponent(*b, 0u) +
                            ReadComponent(*a, 1u) * ReadComponent(*b, 1u) +
                            ReadComponent(*a, 2u) * ReadComponent(*b, 2u);
                    } else {
                        for (uint32_t component = 0u;
                            component < componentCount; ++component) {
                            const float left = ReadComponent(*a, component);
                            const float right = b.has_value()
                                ? ReadComponent(*b, component) : 0.0f;
                            if (node->kind == MaterialNodeKind::Add) {
                                components[component] = left + right;
                            } else if (node->kind == MaterialNodeKind::Multiply) {
                                components[component] = left * right;
                            } else if (node->kind == MaterialNodeKind::Lerp) {
                                components[component] = left +
                                    (right - left) * ReadComponent(*c, component);
                            } else if (node->kind == MaterialNodeKind::Clamp) {
                                components[component] = std::clamp(left, right,
                                    ReadComponent(*c, component));
                            } else if (node->kind == MaterialNodeKind::Saturate) {
                                components[component] =
                                    std::clamp(left, 0.0f, 1.0f);
                            } else if (node->kind == MaterialNodeKind::OneMinus) {
                                components[component] = 1.0f - left;
                            } else if (node->kind == MaterialNodeKind::Power) {
                                components[component] = std::pow(
                                    (std::max)(left, 0.0f), right);
                            }
                        }
                    }
                    result = value;
                }
            }

            evaluating.erase(key);
            if (result.has_value() &&
                (result->type == MaterialValueType::Float1 ||
                 result->type == MaterialValueType::Float2 ||
                 result->type == MaterialValueType::Float3 ||
                 result->type == MaterialValueType::Float4)) {
                memo.emplace(key, *result);
                return result;
            }
            return std::nullopt;
        }

        const MaterialGraphNodeData* FindConnectedPbrSurface(
            const MaterialGraphData& graph) noexcept {
            const MaterialGraphEdgeData* outputEdge = FindInputEdge(
                graph, graph.outputNodeId, "Surface");
            if (outputEdge == nullptr) {
                return nullptr;
            }
            const MaterialGraphNodeData* surface =
                FindNode(graph, outputEdge->sourceNodeId);
            return surface != nullptr &&
                    surface->kind == MaterialNodeKind::PbrSurface
                ? surface : nullptr;
        }

        void ProjectConnectedNumericInput(
            const MaterialGraphData& graph,
            const MaterialGraphNodeData& surface,
            std::string_view inputPin,
            MATH::Vec4& outValue) {
            const MaterialGraphEdgeData* edge =
                FindInputEdge(graph, surface.id, inputPin);
            if (edge == nullptr) {
                return;
            }
            std::unordered_map<std::string, MaterialValueData> memo{};
            std::unordered_set<std::string> evaluating{};
            if (const auto value = EvaluateNumericOutput(
                    graph, edge->sourceNodeId, edge->sourcePin,
                    memo, evaluating)) {
                outValue = {
                    ReadComponent(*value, 0u), ReadComponent(*value, 1u),
                    ReadComponent(*value, 2u), ReadComponent(*value, 3u)
                };
            }
        }

        struct TextureExpressionProjection final {
            const MaterialGraphResourceData* resource = nullptr;
            MATH::Vec4 factor{ 1.0f, 1.0f, 1.0f, 1.0f };
            bool hasAuthoredFactor = false;
        };

        std::optional<TextureExpressionProjection>
        EvaluateTextureExpression(
            const MaterialGraphData& graph,
            std::string_view nodeId,
            std::unordered_set<std::string>& visiting) {
            if (!visiting.emplace(nodeId).second) {
                return std::nullopt;
            }
            const MaterialGraphNodeData* node = FindNode(graph, nodeId);
            if (node == nullptr) {
                visiting.erase(std::string(nodeId));
                return std::nullopt;
            }
            if (node->kind == MaterialNodeKind::TextureSample) {
                if (FindInputEdge(graph, node->id, "UV") != nullptr) {
                    visiting.erase(std::string(nodeId));
                    return std::nullopt;
                }
                const MaterialGraphResourceData* resource =
                    FindResource(graph, node->referenceId);
                visiting.erase(std::string(nodeId));
                return resource == nullptr
                    ? std::nullopt
                    : std::optional<TextureExpressionProjection>{
                        TextureExpressionProjection{ resource }};
            }
            if (node->kind == MaterialNodeKind::NormalUnpack) {
                const MaterialGraphEdgeData* packed =
                    FindInputEdge(graph, node->id, "PackedNormal");
                auto result = packed == nullptr
                    ? std::optional<TextureExpressionProjection>{}
                    : EvaluateTextureExpression(
                        graph, packed->sourceNodeId, visiting);
                visiting.erase(std::string(nodeId));
                return result;
            }
            if (node->kind != MaterialNodeKind::Multiply) {
                visiting.erase(std::string(nodeId));
                return std::nullopt;
            }

            const MaterialGraphEdgeData* a =
                FindInputEdge(graph, node->id, "A");
            const MaterialGraphEdgeData* b =
                FindInputEdge(graph, node->id, "B");
            if (a == nullptr || b == nullptr) {
                visiting.erase(std::string(nodeId));
                return std::nullopt;
            }
            const auto multiplyStaticFactor = [&](
                TextureExpressionProjection& texture,
                const MaterialGraphEdgeData& factorEdge) {
                std::unordered_map<std::string, MaterialValueData> memo{};
                std::unordered_set<std::string> evaluating{};
                const auto factor = EvaluateNumericOutput(
                    graph, factorEdge.sourceNodeId, factorEdge.sourcePin,
                    memo, evaluating);
                if (!factor.has_value()) {
                    return false;
                }
                texture.factor = {
                    texture.factor.x * ReadComponent(*factor, 0u),
                    texture.factor.y * ReadComponent(*factor, 1u),
                    texture.factor.z * ReadComponent(*factor, 2u),
                    texture.factor.w * ReadComponent(*factor, 3u)
                };
                texture.hasAuthoredFactor = true;
                return true;
            };
            auto result = EvaluateTextureExpression(
                graph, a->sourceNodeId, visiting);
            if (result.has_value() && multiplyStaticFactor(*result, *b)) {
                visiting.erase(std::string(nodeId));
                return result;
            }
            result = EvaluateTextureExpression(
                graph, b->sourceNodeId, visiting);
            if (result.has_value() && multiplyStaticFactor(*result, *a)) {
                visiting.erase(std::string(nodeId));
                return result;
            }
            visiting.erase(std::string(nodeId));
            return std::nullopt;
        }

        bool ProjectConnectedTextureInput(
            const MaterialGraphData& graph,
            const MaterialGraphNodeData& surface,
            std::string_view inputPin,
            MaterialTextureSlotData& outSlot,
            MATH::Vec4* outFactor = nullptr) {
            const MaterialGraphEdgeData* edge =
                FindInputEdge(graph, surface.id, inputPin);
            if (edge == nullptr) {
                return false;
            }
            std::unordered_set<std::string> visiting{};
            const auto projection = EvaluateTextureExpression(
                graph, edge->sourceNodeId, visiting);
            if (!projection.has_value() || projection->resource == nullptr) {
                return false;
            }
            const MaterialGraphResourceData* resource = projection->resource;
            outSlot.useTexture = resource->enabled;
            outSlot.textureAssetGuid = resource->assetGuid;
            outSlot.texCoord = static_cast<int>(resource->texCoord);
            outSlot.uvScale = resource->uvScale;
            outSlot.uvOffset = resource->uvOffset;
            outSlot.uvRotation = resource->uvRotation;
            // A direct Texture Sample means "surface default x texture" in
            // the fixed runtime material.  Do not replace the surface value
            // with an implicit 1.0.  Only an explicit Multiply expression
            // authors a replacement factor.
            if (outFactor != nullptr && projection->hasAuthoredFactor) {
                *outFactor = projection->factor;
            }
            return true;
        }

        void ProjectParameter(
            const MaterialGraphData& graph,
            std::string_view id,
            MATH::Vec4& outValue) noexcept {
            if (const MaterialGraphParameterData* parameter =
                    FindParameter(graph, id)) {
                outValue = parameter->defaultValue.numeric;
            }
        }

    } // namespace

    PbrMaterialAssetData ProjectPbrMaterialAssetData(
        const MaterialAssetData& data) {
        PbrMaterialAssetData projected = data.pbr;
        projected.version = data.version;
        projected.materialName = data.materialName;
        projected.doubleSided = data.surface.doubleSided;
        projected.unlit = data.surface.unlit;
        if (data.kind != MaterialAssetKind::Material) {
            return projected;
        }

        MATH::Vec4 value = projected.baseColorFactor;
        ProjectParameter(data.graph, "parameter.baseColor", value);
        projected.baseColorFactor = value;
        value = { projected.metallicFactor, 0.0f, 0.0f, 0.0f };
        ProjectParameter(data.graph, "parameter.metallic", value);
        projected.metallicFactor = value.x;
        value = { projected.roughnessFactor, 0.0f, 0.0f, 0.0f };
        ProjectParameter(data.graph, "parameter.roughness", value);
        projected.roughnessFactor = value.x;
        value = { projected.specularFactor, 0.0f, 0.0f, 0.0f };
        ProjectParameter(data.graph, "parameter.specular", value);
        projected.specularFactor = value.x;
        value = { projected.specularColorFactor.x,
            projected.specularColorFactor.y,
            projected.specularColorFactor.z, 0.0f };
        ProjectParameter(data.graph, "parameter.specularColor", value);
        projected.specularColorFactor = { value.x, value.y, value.z };
        value = { projected.normalScale, 0.0f, 0.0f, 0.0f };
        ProjectParameter(data.graph, "parameter.normalScale", value);
        projected.normalScale = value.x;
        value = { projected.occlusionStrength, 0.0f, 0.0f, 0.0f };
        ProjectParameter(data.graph, "parameter.occlusionStrength", value);
        projected.occlusionStrength = value.x;
        value = { projected.emissiveFactor.x, projected.emissiveFactor.y,
            projected.emissiveFactor.z, 0.0f };
        ProjectParameter(data.graph, "parameter.emissive", value);
        projected.emissiveFactor = { value.x, value.y, value.z };
        value = { projected.emissiveStrength, 0.0f, 0.0f, 0.0f };
        ProjectParameter(data.graph, "parameter.emissiveStrength", value);
        projected.emissiveStrength = value.x;

        const MaterialGraphNodeData* surface =
            FindConnectedPbrSurface(data.graph);
        if (surface == nullptr) {
            return projected;
        }
        auto projectNumeric = [&](std::string_view inputPin,
            MATH::Vec4& projectedValue) {
            ProjectConnectedNumericInput(
                data.graph, *surface, inputPin, projectedValue);
        };
        value = projected.baseColorFactor;
        projectNumeric("BaseColor", value);
        projected.baseColorFactor = value;
        value = { projected.metallicFactor, 0.0f, 0.0f, 0.0f };
        projectNumeric("Metallic", value);
        projected.metallicFactor = value.x;
        value = { projected.roughnessFactor, 0.0f, 0.0f, 0.0f };
        projectNumeric("Roughness", value);
        projected.roughnessFactor = value.x;
        value = { projected.occlusionStrength, 0.0f, 0.0f, 0.0f };
        projectNumeric("Occlusion", value);
        projected.occlusionStrength = value.x;
        value = { projected.emissiveFactor.x, projected.emissiveFactor.y,
            projected.emissiveFactor.z, 0.0f };
        projectNumeric("Emissive", value);
        projected.emissiveFactor = { value.x, value.y, value.z };
        value = { projected.specularFactor, 0.0f, 0.0f, 0.0f };
        projectNumeric("Specular", value);
        projected.specularFactor = value.x;
        value = { projected.specularColorFactor.x,
            projected.specularColorFactor.y,
            projected.specularColorFactor.z, 0.0f };
        projectNumeric("SpecularColor", value);
        projected.specularColorFactor = { value.x, value.y, value.z };

        value = projected.baseColorFactor;
        if (ProjectConnectedTextureInput(
                data.graph, *surface, "BaseColor",
                projected.baseColorTexture, &value)) {
            projected.baseColorFactor = value;
        }
        ProjectConnectedTextureInput(data.graph, *surface, "Normal",
            projected.normalTexture);
        value = { projected.metallicFactor, 0.0f, 0.0f, 0.0f };
        if (ProjectConnectedTextureInput(
                data.graph, *surface, "Metallic",
                projected.metallicRoughnessTexture, &value)) {
            projected.metallicFactor = value.x;
        }
        value = { projected.roughnessFactor, 0.0f, 0.0f, 0.0f };
        if (ProjectConnectedTextureInput(
                data.graph, *surface, "Roughness",
                projected.metallicRoughnessTexture, &value)) {
            projected.roughnessFactor = value.x;
        }
        value = { projected.occlusionStrength, 0.0f, 0.0f, 0.0f };
        if (ProjectConnectedTextureInput(
                data.graph, *surface, "Occlusion",
                projected.occlusionTexture, &value)) {
            projected.occlusionStrength = value.x;
        }
        value = { projected.emissiveFactor.x, projected.emissiveFactor.y,
            projected.emissiveFactor.z, 0.0f };
        if (ProjectConnectedTextureInput(
                data.graph, *surface, "Emissive",
                projected.emissiveTexture, &value)) {
            projected.emissiveFactor = { value.x, value.y, value.z };
        }
        value = { projected.specularFactor, 0.0f, 0.0f, 0.0f };
        if (ProjectConnectedTextureInput(
                data.graph, *surface, "Specular",
                projected.specularTexture, &value)) {
            projected.specularFactor = value.x;
        }
        value = { projected.specularColorFactor.x,
            projected.specularColorFactor.y,
            projected.specularColorFactor.z, 0.0f };
        if (ProjectConnectedTextureInput(
                data.graph, *surface, "SpecularColor",
                projected.specularColorTexture, &value)) {
            projected.specularColorFactor = { value.x, value.y, value.z };
        }
        return projected;
    }

} // namespace HIKARI
