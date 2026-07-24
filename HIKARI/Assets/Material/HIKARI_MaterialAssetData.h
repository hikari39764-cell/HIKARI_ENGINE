#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    struct MaterialTextureSlotData {
        bool useTexture = false;
        AssetGuid textureAssetGuid{};
        int texCoord = 0;
        MATH::Vec2 uvScale{ 1.0f, 1.0f };
        MATH::Vec2 uvOffset{ 0.0f, 0.0f };
        float uvRotation = 0.0f;
    };

    struct PbrMaterialAssetData {
        uint32_t version = 3;
        std::string materialName = "New Material";

        MaterialTextureSlotData baseColorTexture{};
        MaterialTextureSlotData normalTexture{};
        MaterialTextureSlotData metallicRoughnessTexture{};
        MaterialTextureSlotData occlusionTexture{};
        MaterialTextureSlotData emissiveTexture{};
        MaterialTextureSlotData specularTexture{};
        MaterialTextureSlotData specularColorTexture{};

        MATH::Vec4 baseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        float specularFactor = 1.0f;
        MATH::Vec3 specularColorFactor{ 1.0f, 1.0f, 1.0f };
        float normalScale = 1.0f;
        float occlusionStrength = 1.0f;
        MATH::Vec3 emissiveFactor{ 0.0f, 0.0f, 0.0f };
        float emissiveStrength = 1.0f;

        bool doubleSided = false;
        bool unlit = false;
    };

    bool LoadPbrMaterialAssetData(
        const std::filesystem::path& path,
        PbrMaterialAssetData& outData,
        std::string& outError);

    bool SavePbrMaterialAssetData(
        const std::filesystem::path& path,
        const PbrMaterialAssetData& data,
        std::string& outError);

} // namespace HIKARI
