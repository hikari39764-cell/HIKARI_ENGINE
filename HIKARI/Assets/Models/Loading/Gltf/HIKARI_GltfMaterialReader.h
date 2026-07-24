#pragma once

#include <filesystem>

#include <json.hpp>

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::ASSETS::MODELS::GLTF {

    void ReadMaterials(
        const std::filesystem::path& sourcePath,
        const nlohmann::json& root,
        ModelAsset& asset);

} // namespace HIKARI::ASSETS::MODELS::GLTF
