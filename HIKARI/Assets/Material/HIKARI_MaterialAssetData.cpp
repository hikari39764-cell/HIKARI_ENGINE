#include "HIKARI_MaterialAssetData.h"

#include <algorithm>
#include <string_view>
#include <type_traits>
#include "Assets/Material/Graph/HIKARI_MaterialGraphQuery.h"

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

void ApplyPbrMaterialAssetData(MaterialAssetData& material, const PbrMaterialAssetData& pbr) {
    ApplyPbrAdapterEdits(pbr, material);
}

void SyncPbrFromGraphParameters(MaterialAssetData& material) {
    const auto apply = [&](std::string_view id, auto& target) {
        const auto* parameter = FindMaterialGraphParameter(material.graph, id);
        if (!parameter || !IsMaterialNumericType(parameter->defaultValue.type))
            return;
        const auto& value = parameter->defaultValue;
        using Target = std::remove_reference_t<decltype(target)>;
        if constexpr (std::is_same_v<Target, float>) {
            target = value.numeric.x;
        } else {
            target.x = value.numeric.x;
            target.y = value.numeric.y;
            target.z = value.numeric.z;
            if constexpr (std::is_same_v<Target, MATH::Vec4>) {
                if (value.type == MaterialValueType::Float4)
                    target.w = value.numeric.w;
            }
        }
    };
    apply("parameter.baseColor", material.pbr.baseColorFactor);
    apply("parameter.metallic", material.pbr.metallicFactor);
    apply("parameter.roughness", material.pbr.roughnessFactor);
    apply("parameter.specular", material.pbr.specularFactor);
    apply("parameter.specularColor", material.pbr.specularColorFactor);
    apply("parameter.normalScale", material.pbr.normalScale);
    apply("parameter.occlusionStrength", material.pbr.occlusionStrength);
    apply("parameter.emissive", material.pbr.emissiveFactor);
    apply("parameter.emissiveStrength", material.pbr.emissiveStrength);
}

bool LoadMaterialAssetData(const std::filesystem::path& path, MaterialAssetData& outData, std::string& outError,
                           std::vector<std::string>* outDiagnostics) {
    return MATERIAL_SERIALIZATION::LoadMaterialSourceJson(path, outData, outError, outDiagnostics);
}

bool SaveMaterialAssetData(const std::filesystem::path& path, const MaterialAssetData& data, std::string& outError) {
    return MATERIAL_SERIALIZATION::SaveMaterialSourceJson(path, data, outError);
}

bool LoadPbrMaterialAssetData(const std::filesystem::path& path, PbrMaterialAssetData& outData, std::string& outError) {
    MaterialAssetData source{};
    if (!LoadMaterialAssetData(path, source, outError)) {
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
    if (exists && !LoadMaterialAssetData(path, source, outError)) {
        outError = "refusing to overwrite an unreadable Material v6 source: " + outError;
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
    return SaveMaterialAssetData(path, source, outError);
}

} // namespace HIKARI
