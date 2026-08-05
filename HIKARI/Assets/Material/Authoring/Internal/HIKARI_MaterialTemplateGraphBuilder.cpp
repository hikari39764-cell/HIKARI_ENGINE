#include "Assets/Material/Authoring/Internal/HIKARI_MaterialTemplateGraphBuilder.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace HIKARI::ASSETS::MATERIAL::TEMPLATE_FACTORY_INTERNAL {

    namespace {

        void AddNode(
            MaterialAssetData& data,
            std::string id,
            std::string displayName,
            MaterialNodeKind kind,
            MaterialValueType valueType,
            float x,
            float y,
            std::string referenceId = {}) {

            MaterialGraphNodeData node{};
            node.id = std::move(id);
            node.displayName = std::move(displayName);
            node.kind = kind;
            node.valueType = valueType;
            node.position = { x, y };
            node.referenceId = std::move(referenceId);
            data.graph.nodes.push_back(std::move(node));
        }

        void ConnectNodeInput(
            MaterialAssetData& data,
            std::string id,
            std::string sourceNode,
            std::string sourcePin,
            std::string targetNode,
            std::string targetPin) {

            data.graph.edges.push_back(MaterialGraphEdgeData{
                std::move(id), std::move(sourceNode), std::move(sourcePin),
                std::move(targetNode), std::move(targetPin)
            });
        }

        void ConnectSurfaceInput(
            MaterialAssetData& data,
            std::string id,
            std::string sourceNode,
            std::string sourcePin,
            std::string targetPin) {

            ConnectNodeInput(data, std::move(id), std::move(sourceNode),
                std::move(sourcePin), "node.standardPbr", std::move(targetPin));
        }

        void RetargetEdge(
            MaterialAssetData& data,
            std::string_view edgeId,
            std::string targetNode,
            std::string targetPin) {

            const auto edge = std::find_if(
                data.graph.edges.begin(), data.graph.edges.end(),
                [edgeId](const MaterialGraphEdgeData& value) {
                    return value.id == edgeId;
                });
            if (edge == data.graph.edges.end()) {
                return;
            }
            edge->targetNodeId = std::move(targetNode);
            edge->targetPin = std::move(targetPin);
        }

    } // namespace

    void AddSkinLightingGraph(MaterialAssetData& data) {
        AddNode(data, "node.skinLighting", "Skin Lighting",
            MaterialNodeKind::SkinLighting,
            MaterialValueType::DiffuseResponse, -40.0f, 420.0f);
        RetargetEdge(data, "edge.diffuseRamp.response",
            "node.skinLighting", "DiffuseResponse");
        ConnectSurfaceInput(data, "edge.skinLighting.response",
            "node.skinLighting", "Response", "DiffuseResponse");
    }

    void AddFabricSheenGraph(MaterialAssetData& data) {
        AddNode(data, "node.fabricSheen", "Fabric Sheen",
            MaterialNodeKind::FabricSheen, MaterialValueType::Float3,
            -320.0f, 1320.0f);
        AddNode(data, "node.clothAdditive", "Rim + Fabric Sheen",
            MaterialNodeKind::Add, MaterialValueType::Float3,
            -40.0f, 1040.0f);
        RetargetEdge(data, "edge.fresnelRim.color",
            "node.clothAdditive", "A");
        ConnectNodeInput(data, "edge.fabricSheen.color",
            "node.fabricSheen", "Color", "node.clothAdditive", "B");
        ConnectSurfaceInput(data, "edge.clothAdditive.color",
            "node.clothAdditive", "Result", "Additive");
    }

    void AddHybridLightingGraph(MaterialAssetData& data) {
        AddNode(data, "node.hybridLighting", "Hybrid PBR + NPR",
            MaterialNodeKind::HybridLighting,
            MaterialValueType::DiffuseResponse, -40.0f, 420.0f);
        RetargetEdge(data, "edge.diffuseRamp.response",
            "node.hybridLighting", "DiffuseResponse");
        ConnectSurfaceInput(data, "edge.hybridLighting.response",
            "node.hybridLighting", "Response", "DiffuseResponse");
        AddNode(data, "node.texture.matCap", "MatCap Texture",
            MaterialNodeKind::TextureSample, MaterialValueType::Float4,
            -680.0f, 1320.0f, "resource.matCap");
        AddNode(data, "node.matCap", "MatCap",
            MaterialNodeKind::MatCap, MaterialValueType::Float3,
            -320.0f, 1320.0f);
        ConnectNodeInput(data, "edge.matCap.image",
            "node.texture.matCap", "Color", "node.matCap", "Image");
        AddNode(data, "node.additive", "Rim + MatCap",
            MaterialNodeKind::Add, MaterialValueType::Float3,
            -40.0f, 1040.0f);
        RetargetEdge(data, "edge.fresnelRim.color", "node.additive", "A");
        ConnectNodeInput(data, "edge.matCap.color",
            "node.matCap", "Color", "node.additive", "B");
        ConnectSurfaceInput(data, "edge.additive.color",
            "node.additive", "Result", "Additive");
    }

} // namespace HIKARI::ASSETS::MATERIAL::TEMPLATE_FACTORY_INTERNAL
