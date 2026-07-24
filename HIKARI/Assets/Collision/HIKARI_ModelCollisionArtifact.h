#pragma once

#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetRecord.h"

namespace HIKARI::ASSETS::COLLISION {

    struct ModelCollisionArtifactResult {
        bool success = false;
        bool ready = false;
        bool removeExisting = false;
        uint32_t shapeCount = 0u;
        std::filesystem::path path{};
        std::string message{};
    };

    ModelCollisionArtifactResult BuildModelCollisionArtifact(
        const AssetRecord& record,
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& importedDirectory);

} // namespace HIKARI::ASSETS::COLLISION
