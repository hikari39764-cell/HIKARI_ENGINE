#include "HIKARI_MaterialAssetData.h"

#include <algorithm>

#include <json.hpp>

#include "Core/HIKARI_JsonRead.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"

namespace HIKARI {

    namespace JsonMath = SERIALIZATION::JSON::MATH;

    namespace {
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
                { "useTexture", slot.useTexture },
                { "textureAssetGuid", slot.textureAssetGuid.value },
                { "texCoord", slot.texCoord },
                { "uvScale", JsonMath::ToJsonArray(slot.uvScale) },
                { "uvOffset", JsonMath::ToJsonArray(slot.uvOffset) },
                { "uvRotation", slot.uvRotation }
            };
        }

        PbrMaterialAssetData FromJson(const nlohmann::json& root) {
            PbrMaterialAssetData data{};
            data.version = root.value("version", data.version);
            data.materialName = root.value("materialName", data.materialName);

            if (auto baseColor = root.find("baseColor"); baseColor != root.end() && baseColor->is_object()) {
                data.baseColorTexture = ReadSlot(*baseColor);
                data.baseColorFactor = JSONREAD::Vec4Or(baseColor->value("factor", nlohmann::json::array()), data.baseColorFactor);
            }
            if (auto normal = root.find("normal"); normal != root.end() && normal->is_object()) {
                data.normalTexture = ReadSlot(*normal);
                data.normalScale = normal->value("scale", data.normalScale);
            }
            if (auto mr = root.find("metallicRoughness"); mr != root.end() && mr->is_object()) {
                data.metallicRoughnessTexture = ReadSlot(*mr);
                data.metallicFactor = mr->value("metallicFactor", data.metallicFactor);
                data.roughnessFactor = mr->value("roughnessFactor", data.roughnessFactor);
            }
            if (auto occlusion = root.find("occlusion"); occlusion != root.end() && occlusion->is_object()) {
                data.occlusionTexture = ReadSlot(*occlusion);
                data.occlusionStrength = occlusion->value("strength", data.occlusionStrength);
            }
            if (auto emissive = root.find("emissive"); emissive != root.end() && emissive->is_object()) {
                data.emissiveTexture = ReadSlot(*emissive);
                data.emissiveFactor = JSONREAD::Vec3Or(emissive->value("factor", nlohmann::json::array()), data.emissiveFactor);
                data.emissiveStrength = emissive->value("strength", data.emissiveStrength);
            }
            if (auto specular = root.find("specular"); specular != root.end() && specular->is_object()) {
                data.specularTexture = ReadSlot(*specular);
                data.specularFactor = specular->value("factor", data.specularFactor);
            }
            if (auto specularColor = root.find("specularColor"); specularColor != root.end() && specularColor->is_object()) {
                data.specularColorTexture = ReadSlot(*specularColor);
                data.specularColorFactor = JSONREAD::Vec3Or(
                    specularColor->value("factor", nlohmann::json::array()),
                    data.specularColorFactor);
            }

            data.doubleSided = root.value("doubleSided", data.doubleSided);
            data.unlit = root.value("unlit", data.unlit);
            return data;
        }

        nlohmann::json ToJson(const PbrMaterialAssetData& data) {
            nlohmann::json root{
                { "version", 3u },
                { "materialName", data.materialName },
                { "shaderModel", "PBR" },
                { "baseColor", WriteSlot(data.baseColorTexture) },
                { "normal", WriteSlot(data.normalTexture) },
                { "metallicRoughness", WriteSlot(data.metallicRoughnessTexture) },
                { "occlusion", WriteSlot(data.occlusionTexture) },
                { "emissive", WriteSlot(data.emissiveTexture) },
                { "specular", WriteSlot(data.specularTexture) },
                { "specularColor", WriteSlot(data.specularColorTexture) },
                { "doubleSided", data.doubleSided },
                { "unlit", data.unlit }
            };

            root["baseColor"]["factor"] =
                JsonMath::ToJsonArray(data.baseColorFactor);
            root["normal"]["scale"] = data.normalScale;
            root["metallicRoughness"]["metallicFactor"] = data.metallicFactor;
            root["metallicRoughness"]["roughnessFactor"] = data.roughnessFactor;
            root["occlusion"]["strength"] = data.occlusionStrength;
            root["emissive"]["factor"] =
                JsonMath::ToJsonArray(data.emissiveFactor);
            root["emissive"]["strength"] = data.emissiveStrength;
            root["specular"]["factor"] = data.specularFactor;
            root["specularColor"]["factor"] =
                JsonMath::ToJsonArray(data.specularColorFactor);
            return root;
        }
    }

    bool LoadPbrMaterialAssetData(
        const std::filesystem::path& path,
        PbrMaterialAssetData& outData,
        std::string& outError) {

        nlohmann::json root{};
        if (!SERIALIZATION::JSON::ReadJsonFile(
                path,
                root,
                &outError)) {
            return false;
        }
        if (!root.is_object()) {
            outError = "invalid material JSON: " + path.generic_string();
            return false;
        }

        // Material は Texture の GUID だけを保持し、実パスは Registry 側で解決する。
        outData = FromJson(root);
        return true;
    }

    bool SavePbrMaterialAssetData(
        const std::filesystem::path& path,
        const PbrMaterialAssetData& data,
        std::string& outError) {

        return SERIALIZATION::JSON::WriteJsonFile(
            path,
            ToJson(data),
            &outError);
    }

} // namespace HIKARI
