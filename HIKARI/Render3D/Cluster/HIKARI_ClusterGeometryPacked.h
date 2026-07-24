#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Render3D/Cluster/HIKARI_ClusterGpuData.h"
#include "Render3D/Cluster/HIKARI_ClusteredGeometryAsset.h"

namespace HIKARI::RENDER3D::CLUSTER {

    struct ClusterGeometryPackedLayout {
        uint32_t surfaceCount = 0;
        uint32_t surfaceLodRangeCount = 0;
        uint32_t surfaceSectionCount = 0;
        uint32_t clusterCount = 0;
        uint32_t pageCount = 0;
        uint32_t vertexCount = 0;
        uint32_t skinVertexCount = 0;
        uint32_t indexCount = 0;
        uint32_t meshletPrimitiveCount = 0;
        uint32_t materialSlotCount = 0;

        uint32_t surfaceOffsetBytes = 0;
        uint32_t surfaceLodRangeOffsetBytes = 0;
        uint32_t surfaceSectionOffsetBytes = 0;
        uint32_t clusterOffsetBytes = 0;
        uint32_t pageOffsetBytes = 0;
        uint32_t vertexOffsetBytes = 0;
        uint32_t skinVertexOffsetBytes = 0;
        uint32_t indexOffsetBytes = 0;
        uint32_t meshletPrimitiveOffsetBytes = 0;
        uint32_t materialSlotOffsetBytes = 0;

        uint32_t totalTriangleCount = 0;
        uint32_t totalVertexCount = 0;
        uint32_t flags = 0;
        uint32_t byteSize = 0;

        MATH::Vec4 localBoundsMin{};
        MATH::Vec4 localBoundsMax{};
    };

    struct ClusterGeometryPackedBytes {
        std::vector<uint8_t> geometryBytes{};
        std::vector<uint8_t> metadataBytes{};
        ClusterGeometryPackedLayout layout{};
        uint64_t metadataByteSize = 0;
        std::vector<ClusterGeometrySurfaceRange> surfaceRanges{};
        std::vector<ClusterGeometrySurfaceLodRange> surfaceLodRanges{};
        std::vector<ClusterGeometrySurfaceSection> surfaceSections{};
    };

    struct ClusterGeometryPackOptions {
        bool includeFallbackIndices = true;
    };

    bool PackClusterGeometryForGpu(
        const ClusteredGeometryAsset& asset,
        const ClusterGeometryPackOptions& options,
        ClusterGeometryPackedBytes& outPacked,
        std::string* outMessage = nullptr);

} // namespace HIKARI::RENDER3D::CLUSTER
