#include "HIKARI_MaterialPbrJson.h"

#include <algorithm>
#include <utility>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"

namespace HIKARI::MATERIAL_SERIALIZATION {

namespace {

namespace JsonMath = SERIALIZATION::JSON::MATH;

MaterialTextureSlotData ReadSlot(const nlohmann::json& node) {
    MaterialTextureSlotData slot{};
    if (!node.is_object()) {
        return slot;
    }
    slot.useTexture = node.value("useTexture", false);
    slot.textureAssetGuid.value = node.value("textureAssetGuid", std::string{});
    slot.texCoord = std::clamp(node.value("texCoord", slot.texCoord), 0, 1);
    slot.uvScale = JSONREAD::Vec2Or(node.value("uvScale", nlohmann::json::array()), slot.uvScale);
    slot.uvOffset = JSONREAD::Vec2Or(node.value("uvOffset", nlohmann::json::array()), slot.uvOffset);
    slot.uvRotation = node.value("uvRotation", slot.uvRotation);
    return slot;
}

nlohmann::json WriteSlot(const MaterialTextureSlotData& slot) {
    return nlohmann::json{
        {"useTexture", slot.useTexture},
        {"textureAssetGuid", slot.textureAssetGuid.value},
        {"texCoord", slot.texCoord},
        {"uvScale", JsonMath::ToJsonArray(slot.uvScale)},
        {"uvOffset", JsonMath::ToJsonArray(slot.uvOffset)},
        {"uvRotation", slot.uvRotation},
    };
}

} // namespace

PbrMaterialAssetData ReadPbrMaterialJson(const nlohmann::json& node, std::string materialName) {
    PbrMaterialAssetData data{};
    data.version = kMaterialAssetSourceVersion;
    data.materialName = std::move(materialName);
    if (!node.is_object()) {
        return data;
    }
    if (const auto field = node.find("baseColor"); field != node.end() && field->is_object()) {
        data.baseColorTexture = ReadSlot(*field);
        data.baseColorFactor = JSONREAD::Vec4Or(field->value("factor", nlohmann::json::array()), data.baseColorFactor);
    }
    if (const auto field = node.find("normal"); field != node.end() && field->is_object()) {
        data.normalTexture = ReadSlot(*field);
        data.normalScale = field->value("scale", data.normalScale);
    }
    if (const auto field = node.find("metallicRoughness"); field != node.end() && field->is_object()) {
        data.metallicRoughnessTexture = ReadSlot(*field);
        data.metallicFactor = field->value("metallicFactor", data.metallicFactor);
        data.roughnessFactor = field->value("roughnessFactor", data.roughnessFactor);
    }
    if (const auto field = node.find("occlusion"); field != node.end() && field->is_object()) {
        data.occlusionTexture = ReadSlot(*field);
        data.occlusionStrength = field->value("strength", data.occlusionStrength);
    }
    if (const auto field = node.find("emissive"); field != node.end() && field->is_object()) {
        data.emissiveTexture = ReadSlot(*field);
        data.emissiveFactor = JSONREAD::Vec3Or(field->value("factor", nlohmann::json::array()), data.emissiveFactor);
        data.emissiveStrength = field->value("strength", data.emissiveStrength);
    }
    if (const auto field = node.find("specular"); field != node.end() && field->is_object()) {
        data.specularTexture = ReadSlot(*field);
        data.specularFactor = field->value("factor", data.specularFactor);
    }
    if (const auto field = node.find("specularColor"); field != node.end() && field->is_object()) {
        data.specularColorTexture = ReadSlot(*field);
        data.specularColorFactor =
            JSONREAD::Vec3Or(field->value("factor", nlohmann::json::array()), data.specularColorFactor);
    }
    if (const auto field = node.find("clearCoat"); field != node.end() && field->is_object()) {
        data.clearCoatFactor = field->value("factor", data.clearCoatFactor);
        data.clearCoatRoughnessFactor = field->value("roughness", data.clearCoatRoughnessFactor);
    }
    if (const auto field = node.find("transmission"); field != node.end() && field->is_object()) {
        data.transmissionFactor = field->value("factor", data.transmissionFactor);
        data.ior = field->value("ior", data.ior);
    }
    if (const auto field = node.find("subsurface"); field != node.end() && field->is_object()) {
        data.subsurfaceFactor = field->value("factor", data.subsurfaceFactor);
        data.subsurfaceRadius = field->value("radius", data.subsurfaceRadius);
    }
    data.doubleSided = node.value("doubleSided", data.doubleSided);
    data.unlit = node.value("unlit", data.unlit);
    return data;
}

nlohmann::json WritePbrMaterialJson(const PbrMaterialAssetData& data) {
    nlohmann::json node{
        {"baseColor", WriteSlot(data.baseColorTexture)},
        {"normal", WriteSlot(data.normalTexture)},
        {"metallicRoughness", WriteSlot(data.metallicRoughnessTexture)},
        {"occlusion", WriteSlot(data.occlusionTexture)},
        {"emissive", WriteSlot(data.emissiveTexture)},
        {"specular", WriteSlot(data.specularTexture)},
        {"specularColor", WriteSlot(data.specularColorTexture)},
    };
    node["baseColor"]["factor"] = JsonMath::ToJsonArray(data.baseColorFactor);
    node["normal"]["scale"] = data.normalScale;
    node["metallicRoughness"]["metallicFactor"] = data.metallicFactor;
    node["metallicRoughness"]["roughnessFactor"] = data.roughnessFactor;
    node["occlusion"]["strength"] = data.occlusionStrength;
    node["emissive"]["factor"] = JsonMath::ToJsonArray(data.emissiveFactor);
    node["emissive"]["strength"] = data.emissiveStrength;
    node["specular"]["factor"] = data.specularFactor;
    node["specularColor"]["factor"] = JsonMath::ToJsonArray(data.specularColorFactor);
    node["clearCoat"] = {{"factor", data.clearCoatFactor}, {"roughness", data.clearCoatRoughnessFactor}};
    node["transmission"] = {{"factor", data.transmissionFactor}, {"ior", data.ior}};
    node["subsurface"] = {{"factor", data.subsurfaceFactor}, {"radius", data.subsurfaceRadius}};
    return node;
}

} // namespace HIKARI::MATERIAL_SERIALIZATION
