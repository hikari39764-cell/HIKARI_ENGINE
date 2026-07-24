#pragma once

#include <cstdint>
#include <vector>

#include "Render3D/Cluster/HIKARI_ClusteredGeometryAsset.h"

namespace HIKARI::TOOLS::GEOMETRY {

    struct MeshLodGeneratorSettings {
        uint32_t lodIndex = 0;
        float targetTriangleRatio = 0.75f;
        float targetError = 0.006f;
        bool lockOpenBorders = true;
        bool preserveAttributes = true;
        bool optimizeVertexCache = true;
        bool allowAttributeSeamCollapse = false;
        bool protectGeometricBorders = false;
        bool pruneIsolatedComponents = false;
    };

    struct MeshLodGeneratorResult {
        std::vector<RENDER3D::CLUSTER::ClusterVertex> vertices{};
        std::vector<uint32_t> indices{};
        float geometricError = 0.0f;
        bool simplified = false;
    };

    bool GenerateClusterSurfaceLod(
        const std::vector<RENDER3D::CLUSTER::ClusterVertex>& sourceVertices,
        const std::vector<uint32_t>& sourceIndices,
        const MeshLodGeneratorSettings& settings,
        MeshLodGeneratorResult& outResult);

} // namespace HIKARI::TOOLS::GEOMETRY
