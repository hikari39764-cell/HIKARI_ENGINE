#pragma once

#include <string>
#include <vector>

#include "Render3D/Cluster/HIKARI_ClusteredGeometryAsset.h"

namespace HIKARI::ASSETS::GEOMETRY {

    struct ClusteredGeometryValidationResult {
        bool valid = false;
        uint32_t invalidSurfaceCount = 0;
        uint32_t invalidClusterCount = 0;
        uint32_t invalidPageCount = 0;
        uint32_t invalidBoundsCount = 0;
        uint32_t invalidMaterialCount = 0;
        std::vector<std::string> messages{};
    };

    ClusteredGeometryValidationResult ValidateClusteredGeometryAsset(
        const RENDER3D::CLUSTER::ClusteredGeometryAsset& asset);

} // namespace HIKARI::ASSETS::GEOMETRY
