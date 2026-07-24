#pragma once

#include <filesystem>
#include <string>

#include "Assets/Collision/HIKARI_CollisionGeometryAsset.h"

namespace HIKARI::ASSETS::COLLISION {

    struct HcollisionReadInfo {
        uint64_t contentHash = 0u;
        uint64_t fileSize = 0u;
    };

    uint64_t ComputeCollisionGeometryContentHash(
        const CollisionGeometryAsset& asset) noexcept;

    bool WriteHcollisionFile(
        const std::filesystem::path& path,
        const CollisionGeometryAsset& asset,
        std::string& outMessage);

    bool ReadHcollisionFile(
        const std::filesystem::path& path,
        CollisionGeometryAsset& outAsset,
        std::string& outMessage,
        HcollisionReadInfo* outInfo = nullptr);

} // namespace HIKARI::ASSETS::COLLISION
