#pragma once

#include <vector>

#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"

namespace HIKARI::ASSETS::GEOMETRY::COOKING {

    using RENDER3D::CLUSTER::ClusterPage;
    using RENDER3D::CLUSTER::ClusterSurface;
    using RENDER3D::CLUSTER::ClusterSurfaceFlags;
    using RENDER3D::CLUSTER::ClusterSurfaceLodRange;
    using RENDER3D::CLUSTER::ClusterSurfaceSection;
    using RENDER3D::CLUSTER::ClusterSkinVertex;
    using RENDER3D::CLUSTER::ClusterVertex;
    using RENDER3D::CLUSTER::ClusteredGeometryAsset;
    using RENDER3D::CLUSTER::ClusteredGeometryBuildReport;
    using RENDER3D::CLUSTER::MeshCluster;
    using RENDER3D::CLUSTER::MeshletPrimitive;

    struct SourceTriangle {
        uint32_t i0 = 0;
        uint32_t i1 = 0;
        uint32_t i2 = 0;
        Bounds bounds{};
        MATH::Vec3 normal{ 0.0f, 1.0f, 0.0f };
        float area = 0.0f;
    };

    struct SurfaceCookInput {
        uint32_t nodeIndex = RENDER3D::CLUSTER::kInvalidClusterIndex;
        uint32_t meshIndex = 0;
        uint32_t primitiveIndex = 0;
        uint32_t materialIndex = 0;
        uint32_t flags = 0;
        std::vector<ClusterVertex> vertices{};
        std::vector<uint32_t> indices{};
    };

    struct SourceGeometryCounts {
        uint32_t staticTriangleCount = 0;
        uint32_t staticVertexCount = 0;
    };

    struct SurfaceLodResult {
        SurfaceCookInput work{};
        float geometricError = 0.0f;
    };

    struct SurfaceSectionPlan {
        std::vector<uint32_t> triangleIndices{};
        std::vector<std::vector<uint32_t>> groups{};
    };

    struct SurfaceShapeAnalysis {
        float maxExtent = 0.0f;
        float midExtent = 0.0f;
        float minExtent = 0.0f;
        float normalCoherence = 0.0f;
        float dominantNormalRatio = 0.0f;
        bool coherentPlanar = false;
        bool largePlanar = false;
    };

    struct TrianglePartitionConfig {
        uint32_t minChunkTriangles = 1;
        uint32_t maxDepth = 1;
        float maxExtent = 1.0f;
        bool planarCoarsened = false;
    };

    SourceGeometryCounts CountSourceGeometry(const ModelAsset& model);
    Bounds ComputeVertexBounds(const std::vector<ClusterVertex>& vertices);
    Bounds MergeBounds(const Bounds& lhs, const Bounds& rhs);
    std::vector<SourceTriangle> BuildTriangles(
        const std::vector<ClusterVertex>& vertices,
        const std::vector<uint32_t>& indices);
    uint32_t CountSurfaceTriangles(const SurfaceCookInput& work);
    SurfaceCookInput SubdivideLargeStaticTriangles(
        const SurfaceCookInput& work,
        const ClusteredGeometryCookSettings& settings,
        ClusteredGeometryBuildReport& report);
    bool BuildSurfaceCookInput(
        const ModelAsset& model,
        const MeshPrimitive& primitive,
        uint32_t nodeIndex,
        int nodeSkinIndex,
        uint32_t meshIndex,
        uint32_t primitiveIndex,
        const MATH::Mat4& matrix,
        const ClusteredGeometryCookSettings& settings,
        SurfaceCookInput& outWork);
    void AppendMaterialSlots(
        const ModelAsset& model,
        ClusteredGeometryAsset& asset);

    bool ShouldUsePermissiveOpaqueLods(uint32_t flags);
    uint32_t TriangleNormalBucket(const SourceTriangle& triangle);
    SurfaceShapeAnalysis AnalyzeSurfaceShape(
        const std::vector<SourceTriangle>& triangles,
        const Bounds& bounds);
    Bounds ComputeTriangleSubsetBounds(
        const std::vector<SourceTriangle>& triangles,
        const std::vector<uint32_t>& triangleIndices);
    bool BuildLargeStaticSurfaceSectionPlans(
        const SurfaceCookInput& work,
        const std::vector<SourceTriangle>& triangles,
        const ClusteredGeometryCookSettings& settings,
        ClusteredGeometryBuildReport& report,
        std::vector<SurfaceSectionPlan>& outPlans);
    SurfaceCookInput BuildSectionCookInput(
        const SurfaceCookInput& source,
        const std::vector<SourceTriangle>& triangles,
        const std::vector<uint32_t>& triangleIndices);
    void ConfigureSectionLodMetric(
        ClusterSurfaceSection& section,
        const ClusteredGeometryCookSettings& settings,
        const Bounds& metricBounds);
    void FinalizeAssetLodMetrics(
        ClusteredGeometryAsset& asset,
        const ClusteredGeometryCookSettings& settings);

    float ResolveLodMinScreenRadius(
        uint32_t lodIndex,
        const ClusteredGeometryCookSettings& settings);
    bool BuildReducedSurfaceLod(
        const SurfaceCookInput& source,
        uint32_t lodIndex,
        uint32_t previousTriangleCount,
        const ClusteredGeometryCookSettings& settings,
        bool lockSectionBorders,
        SurfaceLodResult& outResult);

    std::vector<std::vector<uint32_t>> BuildClusterTriangleGroups(
        const std::vector<ClusterVertex>& vertices,
        const std::vector<SourceTriangle>& triangles,
        const ClusteredGeometryCookSettings& settings,
        ClusteredGeometryBuildReport* report = nullptr);
    bool AssembleClusteredSurface(
        const SurfaceCookInput& work,
        const ClusteredGeometryCookSettings& settings,
        ClusteredGeometryAsset& asset,
        ClusteredGeometryBuildReport& report);

    void FillCookReportFromAsset(
        const ClusteredGeometryAsset& asset,
        ClusteredGeometryBuildReport& report);
    void FillPackedGeometryByteReport(
        const ClusteredGeometryAsset& asset,
        ClusteredGeometryBuildReport& report);
    void ApplyCookBudgetReport(
        const ClusteredGeometryCookSettings& settings,
        ClusteredGeometryBuildReport& report);

} // namespace HIKARI::ASSETS::GEOMETRY::COOKING
