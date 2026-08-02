#include "HIKARI_MaterialAssetData.h"

#include <string_view>
#include <utility>

namespace HIKARI {

    namespace {

        MaterialGraphParameterData MakeParameter(
            std::string id,
            std::string displayName,
            MaterialValueType type,
            const MATH::Vec4& numeric) {

            MaterialGraphParameterData parameter{};
            parameter.id = std::move(id);
            parameter.displayName = std::move(displayName);
            parameter.defaultValue.type = type;
            parameter.defaultValue.numeric = numeric;
            return parameter;
        }

        MaterialGraphResourceData MakeResource(
            std::string id,
            std::string displayName,
            MaterialResourceSemantic semantic,
            const MaterialTextureSlotData& slot) {

            MaterialGraphResourceData resource{};
            resource.id = std::move(id);
            resource.displayName = std::move(displayName);
            resource.semantic = semantic;
            resource.assetGuid = slot.textureAssetGuid;
            resource.enabled = slot.useTexture;
            resource.texCoord = static_cast<uint32_t>(
                slot.texCoord < 0 ? 0 : slot.texCoord);
            resource.uvScale = slot.uvScale;
            resource.uvOffset = slot.uvOffset;
            resource.uvRotation = slot.uvRotation;
            return resource;
        }

    } // namespace

    MaterialAssetData MakeStandardPbrMaterialAssetData(
        const PbrMaterialAssetData& legacyData) {

        MaterialAssetData data{};
        data.version = kMaterialAssetSourceVersion;
        data.sourceVersion = legacyData.version;
        data.migratedFromLegacy = legacyData.version < kMaterialAssetSourceVersion;
        data.materialName = legacyData.materialName;
        data.templateId = MaterialTemplateId::StandardPbr;
        data.surface.doubleSided = legacyData.doubleSided;
        data.surface.unlit = legacyData.unlit;
        data.pbr = legacyData;
        data.pbr.version = kMaterialAssetSourceVersion;

        data.graph.parameters = {
            MakeParameter("parameter.baseColor", "Base Color",
                MaterialValueType::Float4, legacyData.baseColorFactor),
            MakeParameter("parameter.metallic", "Metallic",
                MaterialValueType::Float1,
                { legacyData.metallicFactor, 0.0f, 0.0f, 0.0f }),
            MakeParameter("parameter.roughness", "Roughness",
                MaterialValueType::Float1,
                { legacyData.roughnessFactor, 0.0f, 0.0f, 0.0f }),
            MakeParameter("parameter.specular", "Specular",
                MaterialValueType::Float1,
                { legacyData.specularFactor, 0.0f, 0.0f, 0.0f }),
            MakeParameter("parameter.specularColor", "Specular Color",
                MaterialValueType::Float3,
                { legacyData.specularColorFactor.x,
                    legacyData.specularColorFactor.y,
                    legacyData.specularColorFactor.z, 0.0f }),
            MakeParameter("parameter.normalScale", "Normal Scale",
                MaterialValueType::Float1,
                { legacyData.normalScale, 0.0f, 0.0f, 0.0f }),
            MakeParameter("parameter.occlusionStrength", "Occlusion Strength",
                MaterialValueType::Float1,
                { legacyData.occlusionStrength, 0.0f, 0.0f, 0.0f }),
            MakeParameter("parameter.emissive", "Emissive",
                MaterialValueType::Float3,
                { legacyData.emissiveFactor.x, legacyData.emissiveFactor.y,
                    legacyData.emissiveFactor.z, 0.0f }),
            MakeParameter("parameter.emissiveStrength", "Emissive Strength",
                MaterialValueType::Float1,
                { legacyData.emissiveStrength, 0.0f, 0.0f, 0.0f }),
        };

        data.graph.resources = {
            MakeResource("resource.baseColor", "Base Color",
                MaterialResourceSemantic::BaseColor,
                legacyData.baseColorTexture),
            MakeResource("resource.normal", "Normal",
                MaterialResourceSemantic::Normal,
                legacyData.normalTexture),
            MakeResource("resource.metallicRoughness", "Metallic Roughness",
                MaterialResourceSemantic::MetallicRoughness,
                legacyData.metallicRoughnessTexture),
            MakeResource("resource.occlusion", "Occlusion",
                MaterialResourceSemantic::Occlusion,
                legacyData.occlusionTexture),
            MakeResource("resource.emissive", "Emissive",
                MaterialResourceSemantic::Emissive,
                legacyData.emissiveTexture),
            MakeResource("resource.specular", "Specular",
                MaterialResourceSemantic::Specular,
                legacyData.specularTexture),
            MakeResource("resource.specularColor", "Specular Color",
                MaterialResourceSemantic::SpecularColor,
                legacyData.specularColorTexture),
        };

        auto addNode = [&data](
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
        };
        auto connect = [&data](
            std::string id,
            std::string sourceNode,
            std::string sourcePin,
            std::string targetNode,
            std::string targetPin) {
            data.graph.edges.push_back(MaterialGraphEdgeData{
                std::move(id), std::move(sourceNode), std::move(sourcePin),
                std::move(targetNode), std::move(targetPin)
            });
        };
        addNode("node.standardPbr", "Principled PBR BSDF",
            MaterialNodeKind::PbrSurface, MaterialValueType::Surface,
            0.0f, 0.0f);
        addNode("node.surfaceOutput", "Surface Output",
            MaterialNodeKind::SurfaceOutput, MaterialValueType::Surface,
            360.0f, 0.0f);

        // Principled PBR owns the canonical factor and texture defaults above.
        // Explicit graph inputs remain available and override those defaults,
        // but a new material no longer expands every built-in input into a
        // Parameter -> Texture -> Multiply implementation subgraph.
        connect("edge.standardPbr.surfaceOutput", "node.standardPbr",
            "Surface", "node.surfaceOutput", "Surface");
        data.graph.outputNodeId = "node.surfaceOutput";
        return data;
    }

} // namespace HIKARI
