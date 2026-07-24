#include "Render3D/Cluster/HIKARI_ClusteredGeometryAsset.h"

#include <algorithm>

namespace HIKARI::RENDER3D::CLUSTER {

    uint32_t CountClusterTriangles(const ClusteredGeometryAsset& asset) {
        uint64_t total = 0;
        for (const MeshCluster& cluster : asset.clusters) {
            total += cluster.triangleCount;
        }
        return static_cast<uint32_t>((std::min<uint64_t>)(total, 0xffffffffull));
    }

    uint32_t CountMaxClusterVertices(const ClusteredGeometryAsset& asset) {
        uint32_t maxValue = 0;
        for (const MeshCluster& cluster : asset.clusters) {
            maxValue = (std::max)(maxValue, cluster.vertexCount);
        }
        return maxValue;
    }

} // namespace HIKARI::RENDER3D::CLUSTER
