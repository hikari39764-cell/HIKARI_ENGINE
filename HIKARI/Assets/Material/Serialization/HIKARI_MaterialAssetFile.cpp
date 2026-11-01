#include "Assets/Material/Serialization/HIKARI_MaterialAssetFile.h"

#include <algorithm>
#include <string_view>

#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"

namespace HIKARI {

namespace {

void UpdateParameter(MaterialGraphData& graph, std::string_view id, MaterialValueType type, const MATH::Vec4& numeric) {
    const auto parameter =
        std::find_if(graph.parameters.begin(), graph.parameters.end(),
                     [id](const MaterialGraphParameterData& candidate) { return candidate.id == id; });
    if (parameter != graph.parameters.end()) {
        parameter->defaultValue.type = type;
        parameter->defaultValue.numeric = numeric;
    }
}

void UpdateResource(MaterialGraphData& graph, MaterialResourceSemantic semantic, const MaterialTextureSlotData& slot) {
    const auto resource =
        std::find_if(graph.resources.begin(), graph.resources.end(),
                     [semantic](const MaterialGraphResourceData& candidate) { return candidate.semantic == semantic; });
    if (resource == graph.resources.end()) {
        return;
    }
    resource->assetGuid = slot.textureAssetGuid;
    resource->enabled = slot.useTexture;
    resource->texCoord = static_cast<uint32_t>(slot.texCoord < 0 ? 0 : slot.texCoord);
    resource->uvScale = slot.uvScale;
    resource->uvOffset = slot.uvOffset;
    resource->uvRotation = slot.uvRotation;
}

void ApplyPbrAdapterEdits(const PbrMaterialAssetData& pbr, MaterialAssetData& source) {
    source.materialName = pbr.materialName;
    source.pbr = pbr;
    source.pbr.version = kMaterialAssetSourceVersion;
    source.surface.doubleSided = pbr.doubleSided;
    source.surface.unlit = pbr.unlit;
    UpdateParameter(source.graph, "parameter.baseColor", MaterialValueType::Float4, pbr.baseColorFactor);
    UpdateParameter(source.graph, "parameter.metallic", MaterialValueType::Float1,
                    {pbr.metallicFactor, 0.0f, 0.0f, 0.0f});
    UpdateParameter(source.graph, "parameter.roughness", MaterialValueType::Float1,
                    {pbr.roughnessFactor, 0.0f, 0.0f, 0.0f});
    UpdateParameter(source.graph, "parameter.specular", MaterialValueType::Float1,
                    {pbr.specularFactor, 0.0f, 0.0f, 0.0f});
    UpdateParameter(source.graph, "parameter.specularColor", MaterialValueType::Float3,
                    {pbr.specularColorFactor.x, pbr.specularColorFactor.y, pbr.specularColorFactor.z, 0.0f});
    UpdateParameter(source.graph, "parameter.normalScale", MaterialValueType::Float1,
                    {pbr.normalScale, 0.0f, 0.0f, 0.0f});
    UpdateParameter(source.graph, "parameter.occlusionStrength", MaterialValueType::Float1,
                    {pbr.occlusionStrength, 0.0f, 0.0f, 0.0f});
    UpdateParameter(source.graph, "parameter.emissive", MaterialValueType::Float3,
                    {pbr.emissiveFactor.x, pbr.emissiveFactor.y, pbr.emissiveFactor.z, 0.0f});
    UpdateParameter(source.graph, "parameter.emissiveStrength", MaterialValueType::Float1,
                    {pbr.emissiveStrength, 0.0f, 0.0f, 0.0f});

    UpdateResource(source.graph, MaterialResourceSemantic::BaseColor, pbr.baseColorTexture);
    UpdateResource(source.graph, MaterialResourceSemantic::Normal, pbr.normalTexture);
    UpdateResource(source.graph, MaterialResourceSemantic::MetallicRoughness, pbr.metallicRoughnessTexture);
    UpdateResource(source.graph, MaterialResourceSemantic::Occlusion, pbr.occlusionTexture);
    UpdateResource(source.graph, MaterialResourceSemantic::Emissive, pbr.emissiveTexture);
    UpdateResource(source.graph, MaterialResourceSemantic::Specular, pbr.specularTexture);
    UpdateResource(source.graph, MaterialResourceSemantic::SpecularColor, pbr.specularColorTexture);
}

} // namespace

bool LoadPbrMaterialAssetData(const std::filesystem::path& path, PbrMaterialAssetData& outData, std::string& outError) {
    MaterialAssetData source{};
    if (!MATERIAL_SERIALIZATION::LoadMaterialSourceJson(path, source, outError)) {
        return false;
    }
    if (source.kind != MaterialAssetKind::Material && source.kind != MaterialAssetKind::Instance) {
        outError = "material source is not a surface Material or Instance: " + path.generic_string();
        return false;
    }
    outData = ProjectPbrMaterialAssetData(source);
    return true;
}

bool SavePbrMaterialAssetData(const std::filesystem::path& path, const PbrMaterialAssetData& data,
                              std::string& outError) {
    MaterialAssetData source{};
    std::error_code existsError{};
    const bool exists = std::filesystem::is_regular_file(path, existsError);
    if (exists && !MATERIAL_SERIALIZATION::LoadMaterialSourceJson(path, source, outError)) {
        outError = "refusing to overwrite an unreadable Material source: " + outError;
        return false;
    }
    if (exists && source.kind != MaterialAssetKind::Material) {
        outError = "surface material editing cannot overwrite a Material Function, Instance, Profile, or Ramp";
        return false;
    }
    if (!exists) {
        source = MakeStandardPbrMaterialAssetData(data);
    } else {
        ApplyPbrAdapterEdits(data, source);
    }
    return MATERIAL_SERIALIZATION::SaveMaterialSourceJson(path, source, outError);
}

} // namespace HIKARI
