#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Shaders/Include/HIKARI_ClusterGeometryConfig.h"

namespace HIKARI::RENDER3D::CLUSTER {

    constexpr uint32_t kInvalidClusterIndex = (std::numeric_limits<uint32_t>::max)();
    constexpr uint32_t kHcmeshMaxTrianglesPerCluster =
        HIKARI_CLUSTER_GEOMETRY_CONFIG_MAX_MESHLET_TRIANGLES;
    constexpr uint32_t kHcmeshMaxVerticesPerCluster =
        HIKARI_CLUSTER_GEOMETRY_CONFIG_MAX_MESHLET_VERTICES;
    constexpr uint32_t kHcmeshMaxClustersPerPage =
        HIKARI_CLUSTER_GEOMETRY_CONFIG_AS_CLUSTER_PAYLOAD;
    constexpr uint32_t kHcmeshMaxTrianglesPerMeshlet = kHcmeshMaxTrianglesPerCluster;
    constexpr uint32_t kHcmeshMaxVerticesPerMeshlet = kHcmeshMaxVerticesPerCluster;
    constexpr uint32_t kHcmeshMaxMeshletsPerPage = kHcmeshMaxClustersPerPage;

    static_assert(
        kHcmeshMaxVerticesPerCluster <= 256u,
        "meshlet primitive は 8bit local index で頂点を参照する");
    static_assert(
        kHcmeshMaxTrianglesPerCluster >= kHcmeshMaxVerticesPerCluster,
        "meshlet は三角形出力が律速になる構成 (三角形数 >= 頂点数) を保つ");
    static_assert(
        HIKARI_CLUSTER_GEOMETRY_CONFIG_MESHLET_MS_THREAD_COUNT >=
            kHcmeshMaxTrianglesPerCluster &&
        HIKARI_CLUSTER_GEOMETRY_CONFIG_MESHLET_MS_THREAD_COUNT >=
            kHcmeshMaxVerticesPerCluster &&
        (HIKARI_CLUSTER_GEOMETRY_CONFIG_MESHLET_MS_THREAD_COUNT % 32u) == 0u,
        "MS thread 数は max(頂点, 三角形) 以上の wave 倍数にする");

    enum class ClusterSurfaceFlags : uint32_t {
        None = 0,
        Opaque = 1u << 0,
        AlphaMask = 1u << 1,
        Transparent = 1u << 2,
        DoubleSided = 1u << 3,
        Skinned = 1u << 4,
        Unsupported = 1u << 5,
    };

    inline uint32_t ToBits(ClusterSurfaceFlags value) {
        return static_cast<uint32_t>(value);
    }

    inline bool HasFlag(uint32_t flags, ClusterSurfaceFlags value) {
        return (flags & ToBits(value)) != 0u;
    }

    inline void AddFlag(uint32_t& flags, ClusterSurfaceFlags value) {
        flags |= ToBits(value);
    }

    enum class ClusteredGeometryFlags : uint32_t {
        None = 0,
        NodeTransformBaked = 1u << 0,
        ClusterLocalIndices = 1u << 1,
        SourceMapping = 1u << 2,
        MeshletPrimitiveTable = 1u << 3,
        MeshletReady = 1u << 4,
        LodRanges = 1u << 5,
        SkinningData = 1u << 6,
    };

    inline uint32_t ToBits(ClusteredGeometryFlags value) {
        return static_cast<uint32_t>(value);
    }

    inline bool HasFlag(uint32_t flags, ClusteredGeometryFlags value) {
        return (flags & ToBits(value)) != 0u;
    }

    inline void AddFlag(uint32_t& flags, ClusteredGeometryFlags value) {
        flags |= ToBits(value);
    }

    struct ClusterVertex {
        MATH::Vec3 position{};
        MATH::Vec3 normal{};
        MATH::Vec4 tangent{ 1.0f, 0.0f, 0.0f, 1.0f };
        MATH::Vec2 uv0{};
        MATH::Vec2 uv1{};
        MATH::Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        uint16_t joints[4]{};
        float weights[4]{};
    };

    struct ClusterSkinVertex {
        uint16_t joints[4]{};
        float weights[4]{};
    };

    struct MeshletPrimitive {
        uint32_t i0 = 0;
        uint32_t i1 = 0;
        uint32_t i2 = 0;
        uint32_t reserved0 = 0;
    };

    struct ClusterSurface {
        uint32_t nodeIndex = kInvalidClusterIndex;
        uint32_t meshIndex = 0;
        uint32_t primitiveIndex = 0;
        uint32_t materialIndex = 0;

        uint32_t firstCluster = 0;
        uint32_t clusterCount = 0;

        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;

        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;

        uint32_t firstPage = 0;
        uint32_t pageCount = 0;

        uint32_t firstPrimitive = 0;
        uint32_t primitiveCount = 0;

        uint32_t firstLodRange = 0;
        uint32_t lodRangeCount = 0;
        uint32_t firstSection = 0;
        uint32_t sectionCount = 0;

        Bounds localBounds{};

        uint32_t flags = 0;
    };

    struct ClusterSurfaceLodRange {
        uint32_t surfaceIndex = 0;
        uint32_t lodIndex = 0;

        uint32_t firstCluster = 0;
        uint32_t clusterCount = 0;

        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;

        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;

        uint32_t firstPage = 0;
        uint32_t pageCount = 0;

        uint32_t firstPrimitive = 0;
        uint32_t primitiveCount = 0;

        float geometricError = 0.0f;
        float minScreenRadius = 0.0f;
        uint32_t flags = 0;
        uint32_t sectionIndex = 0;
    };

    struct ClusterSurfaceSection {
        uint32_t surfaceIndex = 0;
        uint32_t sectionIndex = 0;

        uint32_t firstCluster = 0;
        uint32_t clusterCount = 0;

        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;

        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;

        uint32_t firstPage = 0;
        uint32_t pageCount = 0;

        uint32_t firstPrimitive = 0;
        uint32_t primitiveCount = 0;

        uint32_t firstLodRange = 0;
        uint32_t lodRangeCount = 0;

        Bounds localBounds{};
        Bounds lodMetricBounds{};

        uint32_t flags = 0;
        float lodErrorBudgetNdc = 0.006f;
        uint32_t reserved0 = 0;
        uint32_t reserved1 = 0;
    };

    struct MeshCluster {
        uint32_t surfaceIndex = 0;

        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;

        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;

        uint32_t triangleCount = 0;
        uint32_t firstPrimitive = 0;
        uint32_t primitiveCount = 0;

        Bounds localBounds{};

        MATH::Vec3 sphereCenter{};
        float sphereRadius = 0.0f;

        // perspective cone culling 用。meshoptimizer の apex/axis/cutoff をそのまま保持する。
        MATH::Vec3 coneApex{};
        float coneReserved = 0.0f;

        MATH::Vec3 coneAxis{ 0.0f, 1.0f, 0.0f };
        float coneCutoff = 0.0f;

        uint32_t flags = 0;
    };

    struct ClusterPage {
        uint32_t firstCluster = 0;
        uint32_t clusterCount = 0;

        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;

        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;

        uint32_t firstPrimitive = 0;
        uint32_t primitiveCount = 0;

        Bounds localBounds{};
    };

    using MeshletVertex = ClusterVertex;
    using Meshlet = MeshCluster;
    using MeshletPage = ClusterPage;

    struct ClusteredGeometryAsset {
        AssetGuid sourceModelGuid{};
        std::string sourceModelPath{};
        // 頂点空間と source mapping の前提を固定する。
        uint32_t flags = 0;

        std::vector<ClusterSurface> surfaces{};
        std::vector<ClusterSurfaceLodRange> surfaceLodRanges{};
        std::vector<ClusterSurfaceSection> surfaceSections{};
        std::vector<MeshCluster> clusters{};
        std::vector<ClusterPage> pages{};

        std::vector<ClusterVertex> packedVertices{};
        // Skin data is present only for assets that contain skinned surfaces.
        // Indices remain parallel to packedVertices after meshlet remapping.
        std::vector<ClusterSkinVertex> packedSkinningVertices{};
        // index は各 meshlet の firstVertex から見たローカル頂点 index。
        std::vector<uint32_t> packedIndices{};
        // Mesh Shader 用に triangle primitive を直接読める形で保持する。
        std::vector<MeshletPrimitive> meshletPrimitives{};
        std::vector<uint32_t> materialSlotMapping{};

        Bounds localBounds{};

        uint32_t totalTriangleCount = 0;
        uint32_t totalVertexCount = 0;

        uint32_t skippedPrimitiveCount = 0;
        uint32_t skippedSkinnedPrimitiveCount = 0;
        uint32_t skippedMorphPrimitiveCount = 0;
        uint32_t skippedInvalidPrimitiveCount = 0;
        uint32_t unsupportedPrimitiveModeCount = 0;
        uint32_t unsupportedFeatureCount = 0;

        bool valid = false;
    };

    struct ClusteredGeometryBuildReport {
        uint32_t sourceStaticTriangleCount = 0;
        uint32_t sourceStaticVertexCount = 0;
        uint32_t surfaceCount = 0;
        uint32_t surfaceLodRangeCount = 0;
        uint32_t surfaceSectionCount = 0;
        uint32_t clusterCount = 0;
        uint32_t pageCount = 0;
        uint32_t meshletPrimitiveCount = 0;
        uint32_t triangleCount = 0;
        uint32_t vertexCount = 0;
        uint32_t maxVerticesPerCluster = 0;
        uint32_t normalConeValidClusterCount = 0;
        uint32_t normalConeInvalidClusterCount = 0;
        uint32_t normalConeCutoffLeZeroCount = 0;
        uint32_t normalConeCutoffGeOneCount = 0;
        uint32_t normalConeAxisInvalidCount = 0;
        uint32_t skippedSkinnedPrimitiveCount = 0;
        uint32_t skippedMorphPrimitiveCount = 0;
        uint32_t skippedInvalidPrimitiveCount = 0;
        uint32_t unsupportedPrimitiveModeCount = 0;
        uint32_t unsupportedFeatureCount = 0;
        uint32_t partitionedSurfaceCount = 0;
        uint32_t partitionedSurfaceChunkCount = 0;
        uint32_t rejectedPartitionedSurfaceCount = 0;
        uint32_t normalPartitionedSurfaceCount = 0;
        uint32_t normalPartitionedChunkCount = 0;
        uint32_t acceptedNormalBucketGroupCount = 0;
        uint32_t rejectedNormalBucketGroupCount = 0;
        uint32_t compactedClusterGroupCount = 0;
        uint32_t mergedClusterGroupCount = 0;
        uint32_t planarPartitionedSurfaceCount = 0;
        uint32_t planarPartitionedChunkCount = 0;
        uint32_t planarPartitionCoarsenedSurfaceCount = 0;
        uint32_t subdividedSurfaceCount = 0;
        uint32_t subdividedSourceTriangleCount = 0;
        uint32_t subdividedOutputTriangleCount = 0;
        uint32_t planarSubdivisionSkippedSurfaceCount = 0;
        uint32_t planarSubdivisionCoarsenedSurfaceCount = 0;
        uint32_t singleTriangleClusterCount = 0;
        uint32_t lowTriangleClusterCount = 0;
        uint32_t maxTrianglesPerClusterObserved = 0;
        uint64_t packedGeometryByteSize = 0;
        uint64_t packedMetadataByteSize = 0;
        uint64_t packedTotalByteSize = 0;
        uint64_t fallbackIndexByteSize = 0;
        uint64_t meshletPrimitiveByteSize = 0;
        uint64_t packedVertexPositionByteSize = 0;
        uint64_t packedVertexAttributeByteSize = 0;
        uint64_t packedSkinVertexByteSize = 0;
        float averageTrianglesPerCluster = 0.0f;
        float triangleInflationRatio = 0.0f;
        float vertexInflationRatio = 0.0f;
        float normalConeCutoffMin = 0.0f;
        float normalConeCutoffAverage = 0.0f;
        float normalConeCutoffMax = 0.0f;
        bool triangleBudgetExceeded = false;
        bool vertexBudgetExceeded = false;
        bool clusterOccupancyWarning = false;
        std::vector<std::string> messages{};
    };

    uint32_t CountClusterTriangles(const ClusteredGeometryAsset& asset);
    uint32_t CountMaxClusterVertices(const ClusteredGeometryAsset& asset);

} // namespace HIKARI::RENDER3D::CLUSTER
