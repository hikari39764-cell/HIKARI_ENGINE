#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Assets/Models/HIKARI_ModelAssetTypes.h"

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::ASSETS::MODELS::OBJ {

    struct MaterialDescription {
        std::string name;
        MATH::Vec4 baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::string baseColorMapPath;
        std::string normalMapPath;
        std::string roughnessMapPath;
        float metallic = 0.0f;
        float roughness = 1.0f;
        float alpha = 1.0f;
        bool hasAlpha = false;
    };

    using MaterialLibrary = std::unordered_map<std::string, MaterialDescription>;
    using MaterialIndexByName = std::unordered_map<std::string, uint32_t>;
    using TextureIndexByPath = std::unordered_map<std::string, int>;

    void LoadMaterialLibraries(
        const std::vector<std::filesystem::path>& libraryPaths,
        ModelAsset& asset,
        MaterialLibrary& outLibrary);

    uint32_t ResolveMaterialIndex(
        const std::string& materialName,
        const MaterialLibrary& library,
        ModelAsset& asset,
        MaterialIndexByName& materialIndexByName,
        TextureIndexByPath& textureIndexByPath);

} // namespace HIKARI::ASSETS::MODELS::OBJ
