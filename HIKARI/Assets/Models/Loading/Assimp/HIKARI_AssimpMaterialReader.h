#pragma once

#include <filesystem>

struct aiScene;

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::ASSETS::MODELS::ASSIMP {

    void ReadMaterials(
        const aiScene& scene,
        const std::filesystem::path& sourceDirectory,
        ModelAsset& asset);

} // namespace HIKARI::ASSETS::MODELS::ASSIMP
