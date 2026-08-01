#include "HIKARI_MaterialAssetData.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"

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
            resource.texCoord = static_cast<uint32_t>(slot.texCoord < 0 ? 0 : slot.texCoord);
            resource.uvScale = slot.uvScale;
            resource.uvOffset = slot.uvOffset;
            resource.uvRotation = slot.uvRotation;
            return resource;
        }

        void UpdateParameter(
            MaterialGraphData& graph,
            std::string_view id,
            MaterialValueType type,
            const MATH::Vec4& numeric) {

            const auto parameter = std::find_if(
                graph.parameters.begin(),
                graph.parameters.end(),
                [id](const MaterialGraphParameterData& candidate) {
                    return candidate.id == id;
                });
            if (parameter != graph.parameters.end()) {
                parameter->defaultValue.type = type;
                parameter->defaultValue.numeric = numeric;
            }
        }

        void UpdateResource(
            MaterialGraphData& graph,
            MaterialResourceSemantic semantic,
            const MaterialTextureSlotData& slot) {

            const auto resource = std::find_if(
                graph.resources.begin(),
                graph.resources.end(),
                [semantic](const MaterialGraphResourceData& candidate) {
                    return candidate.semantic == semantic;
                });
            if (resource == graph.resources.end()) {
                return;
            }
            resource->assetGuid = slot.textureAssetGuid;
            resource->enabled = slot.useTexture;
            resource->texCoord = static_cast<uint32_t>(
                slot.texCoord < 0 ? 0 : slot.texCoord);
            resource->uvScale = slot.uvScale;
            resource->uvOffset = slot.uvOffset;
            resource->uvRotation = slot.uvRotation;
        }

        void ApplyPbrAdapterEdits(
            const PbrMaterialAssetData& pbr,
            MaterialAssetData& source) {

            source.materialName = pbr.materialName;
            source.pbr = pbr;
            source.pbr.version = kMaterialAssetSourceVersion;
            source.surface.doubleSided = pbr.doubleSided;
            source.surface.unlit = pbr.unlit;
            UpdateParameter(source.graph, "parameter.baseColor",
                MaterialValueType::Float4, pbr.baseColorFactor);
            UpdateParameter(source.graph, "parameter.metallic",
                MaterialValueType::Float1,
                { pbr.metallicFactor, 0.0f, 0.0f, 0.0f });
            UpdateParameter(source.graph, "parameter.roughness",
                MaterialValueType::Float1,
                { pbr.roughnessFactor, 0.0f, 0.0f, 0.0f });
            UpdateParameter(source.graph, "parameter.specular",
                MaterialValueType::Float1,
                { pbr.specularFactor, 0.0f, 0.0f, 0.0f });
            UpdateParameter(source.graph, "parameter.specularColor",
                MaterialValueType::Float3,
                { pbr.specularColorFactor.x, pbr.specularColorFactor.y,
                    pbr.specularColorFactor.z, 0.0f });
            UpdateParameter(source.graph, "parameter.normalScale",
                MaterialValueType::Float1,
                { pbr.normalScale, 0.0f, 0.0f, 0.0f });
            UpdateParameter(source.graph, "parameter.occlusionStrength",
                MaterialValueType::Float1,
                { pbr.occlusionStrength, 0.0f, 0.0f, 0.0f });
            UpdateParameter(source.graph, "parameter.emissive",
                MaterialValueType::Float3,
                { pbr.emissiveFactor.x, pbr.emissiveFactor.y,
                    pbr.emissiveFactor.z, 0.0f });
            UpdateParameter(source.graph, "parameter.emissiveStrength",
                MaterialValueType::Float1,
                { pbr.emissiveStrength, 0.0f, 0.0f, 0.0f });

            UpdateResource(source.graph, MaterialResourceSemantic::BaseColor,
                pbr.baseColorTexture);
            UpdateResource(source.graph, MaterialResourceSemantic::Normal,
                pbr.normalTexture);
            UpdateResource(source.graph,
                MaterialResourceSemantic::MetallicRoughness,
                pbr.metallicRoughnessTexture);
            UpdateResource(source.graph, MaterialResourceSemantic::Occlusion,
                pbr.occlusionTexture);
            UpdateResource(source.graph, MaterialResourceSemantic::Emissive,
                pbr.emissiveTexture);
            UpdateResource(source.graph, MaterialResourceSemantic::Specular,
                pbr.specularTexture);
            UpdateResource(source.graph, MaterialResourceSemantic::SpecularColor,
                pbr.specularColorTexture);
        }

        const MaterialGraphParameterData* FindParameter(
            const MaterialGraphData& graph,
            std::string_view id) noexcept {

            const auto found = std::find_if(
                graph.parameters.begin(),
                graph.parameters.end(),
                [id](const MaterialGraphParameterData& parameter) {
                    return parameter.id == id;
                });
            return found == graph.parameters.end() ? nullptr : &*found;
        }

        const MaterialGraphResourceData* FindResource(
            const MaterialGraphData& graph,
            MaterialResourceSemantic semantic) noexcept {

            const auto found = std::find_if(
                graph.resources.begin(),
                graph.resources.end(),
                [semantic](const MaterialGraphResourceData& resource) {
                    return resource.semantic == semantic;
                });
            return found == graph.resources.end() ? nullptr : &*found;
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

        void ProjectResource(
            const MaterialGraphData& graph,
            MaterialResourceSemantic semantic,
            MaterialTextureSlotData& outSlot) noexcept {

            const MaterialGraphResourceData* resource =
                FindResource(graph, semantic);
            if (resource == nullptr) {
                return;
            }
            outSlot.useTexture = resource->enabled;
            outSlot.textureAssetGuid = resource->assetGuid;
            outSlot.texCoord = static_cast<int>(resource->texCoord);
            outSlot.uvScale = resource->uvScale;
            outSlot.uvOffset = resource->uvOffset;
            outSlot.uvRotation = resource->uvRotation;
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
            MakeParameter("parameter.baseColor", "Base Color", MaterialValueType::Float4,
                legacyData.baseColorFactor),
            MakeParameter("parameter.metallic", "Metallic", MaterialValueType::Float1,
                { legacyData.metallicFactor, 0.0f, 0.0f, 0.0f }),
            MakeParameter("parameter.roughness", "Roughness", MaterialValueType::Float1,
                { legacyData.roughnessFactor, 0.0f, 0.0f, 0.0f }),
            MakeParameter("parameter.specular", "Specular", MaterialValueType::Float1,
                { legacyData.specularFactor, 0.0f, 0.0f, 0.0f }),
            MakeParameter("parameter.specularColor", "Specular Color", MaterialValueType::Float3,
                { legacyData.specularColorFactor.x, legacyData.specularColorFactor.y,
                    legacyData.specularColorFactor.z, 0.0f }),
            MakeParameter("parameter.normalScale", "Normal Scale", MaterialValueType::Float1,
                { legacyData.normalScale, 0.0f, 0.0f, 0.0f }),
            MakeParameter("parameter.occlusionStrength", "Occlusion Strength", MaterialValueType::Float1,
                { legacyData.occlusionStrength, 0.0f, 0.0f, 0.0f }),
            MakeParameter("parameter.emissive", "Emissive", MaterialValueType::Float3,
                { legacyData.emissiveFactor.x, legacyData.emissiveFactor.y,
                    legacyData.emissiveFactor.z, 0.0f }),
            MakeParameter("parameter.emissiveStrength", "Emissive Strength", MaterialValueType::Float1,
                { legacyData.emissiveStrength, 0.0f, 0.0f, 0.0f }),
        };

        data.graph.resources = {
            MakeResource("resource.baseColor", "Base Color", MaterialResourceSemantic::BaseColor,
                legacyData.baseColorTexture),
            MakeResource("resource.normal", "Normal", MaterialResourceSemantic::Normal,
                legacyData.normalTexture),
            MakeResource("resource.metallicRoughness", "Metallic Roughness",
                MaterialResourceSemantic::MetallicRoughness, legacyData.metallicRoughnessTexture),
            MakeResource("resource.occlusion", "Occlusion", MaterialResourceSemantic::Occlusion,
                legacyData.occlusionTexture),
            MakeResource("resource.emissive", "Emissive", MaterialResourceSemantic::Emissive,
                legacyData.emissiveTexture),
            MakeResource("resource.specular", "Specular", MaterialResourceSemantic::Specular,
                legacyData.specularTexture),
            MakeResource("resource.specularColor", "Specular Color",
                MaterialResourceSemantic::SpecularColor, legacyData.specularColorTexture),
        };

        MaterialGraphNodeData pbrNode{};
        pbrNode.id = "node.standardPbr";
        pbrNode.kind = MaterialNodeKind::PbrSurface;
        pbrNode.displayName = "Standard PBR";
        pbrNode.position = { 0.0f, 0.0f };
        pbrNode.valueType = MaterialValueType::Surface;

        MaterialGraphNodeData outputNode{};
        outputNode.id = "node.surfaceOutput";
        outputNode.kind = MaterialNodeKind::SurfaceOutput;
        outputNode.displayName = "Surface Output";
        outputNode.position = { 320.0f, 0.0f };
        outputNode.valueType = MaterialValueType::Surface;

        data.graph.nodes = { std::move(pbrNode), std::move(outputNode) };
        data.graph.edges = { MaterialGraphEdgeData{
            "edge.standardPbr.surfaceOutput",
            "node.standardPbr",
            "Surface",
            "node.surfaceOutput",
            "Surface"
        } };
        data.graph.outputNodeId = "node.surfaceOutput";
        return data;
    }

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

        MATH::Vec4 value{};
        value = projected.baseColorFactor;
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
        value = {
            projected.specularColorFactor.x,
            projected.specularColorFactor.y,
            projected.specularColorFactor.z,
            0.0f
        };
        ProjectParameter(data.graph, "parameter.specularColor", value);
        projected.specularColorFactor = { value.x, value.y, value.z };
        value = { projected.normalScale, 0.0f, 0.0f, 0.0f };
        ProjectParameter(data.graph, "parameter.normalScale", value);
        projected.normalScale = value.x;
        value = { projected.occlusionStrength, 0.0f, 0.0f, 0.0f };
        ProjectParameter(data.graph, "parameter.occlusionStrength", value);
        projected.occlusionStrength = value.x;
        value = {
            projected.emissiveFactor.x,
            projected.emissiveFactor.y,
            projected.emissiveFactor.z,
            0.0f
        };
        ProjectParameter(data.graph, "parameter.emissive", value);
        projected.emissiveFactor = { value.x, value.y, value.z };
        value = { projected.emissiveStrength, 0.0f, 0.0f, 0.0f };
        ProjectParameter(data.graph, "parameter.emissiveStrength", value);
        projected.emissiveStrength = value.x;

        ProjectResource(data.graph, MaterialResourceSemantic::BaseColor,
            projected.baseColorTexture);
        ProjectResource(data.graph, MaterialResourceSemantic::Normal,
            projected.normalTexture);
        ProjectResource(data.graph, MaterialResourceSemantic::MetallicRoughness,
            projected.metallicRoughnessTexture);
        ProjectResource(data.graph, MaterialResourceSemantic::Occlusion,
            projected.occlusionTexture);
        ProjectResource(data.graph, MaterialResourceSemantic::Emissive,
            projected.emissiveTexture);
        ProjectResource(data.graph, MaterialResourceSemantic::Specular,
            projected.specularTexture);
        ProjectResource(data.graph, MaterialResourceSemantic::SpecularColor,
            projected.specularColorTexture);
        return projected;
    }

    bool LoadMaterialAssetData(
        const std::filesystem::path& path,
        MaterialAssetData& outData,
        std::string& outError,
        std::vector<std::string>* outDiagnostics) {

        return MATERIAL_SERIALIZATION::LoadMaterialSourceJson(
            path,
            outData,
            outError,
            outDiagnostics);
    }

    bool SaveMaterialAssetData(
        const std::filesystem::path& path,
        const MaterialAssetData& data,
        std::string& outError) {

        return MATERIAL_SERIALIZATION::SaveMaterialSourceJson(
            path,
            data,
            outError);
    }

    bool LoadPbrMaterialAssetData(
        const std::filesystem::path& path,
        PbrMaterialAssetData& outData,
        std::string& outError) {

        MaterialAssetData source{};
        if (!LoadMaterialAssetData(path, source, outError)) {
            return false;
        }
        if (source.kind != MaterialAssetKind::Material &&
            source.kind != MaterialAssetKind::Instance) {
            outError = "material source is not a surface Material or Instance: " +
                path.generic_string();
            return false;
        }
        outData = ProjectPbrMaterialAssetData(source);
        return true;
    }

    bool SavePbrMaterialAssetData(
        const std::filesystem::path& path,
        const PbrMaterialAssetData& data,
        std::string& outError) {

        MaterialAssetData source{};
        std::error_code existsError{};
        const bool exists = std::filesystem::is_regular_file(path, existsError);
        if (exists && !LoadMaterialAssetData(path, source, outError)) {
            outError = "refusing to overwrite an unreadable Material v4 source through the legacy PBR adapter: " +
                outError;
            return false;
        }
        if (exists && source.kind != MaterialAssetKind::Material) {
            outError = "legacy PBR adapter cannot overwrite a Material Function, Instance, Profile, or Ramp";
            return false;
        }
        if (!exists) {
            source = MakeStandardPbrMaterialAssetData(data);
        } else {
            ApplyPbrAdapterEdits(data, source);
        }
        return SaveMaterialAssetData(path, source, outError);
    }

} // namespace HIKARI
