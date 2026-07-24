#include "Assets/Geometry/Cooking/Internal/HIKARI_ClusteredGeometryCookInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::GEOMETRY::COOKING {

    bool ShouldUsePermissiveOpaqueLods(uint32_t flags) {
        return RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::Opaque) &&
            !RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::AlphaMask) &&
            !RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::Transparent) &&
            !RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::DoubleSided) &&
            !RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::Skinned) &&
            !RENDER3D::CLUSTER::HasFlag(flags, ClusterSurfaceFlags::Unsupported);
    }

    float BoundsExtentOnAxis(const Bounds& bounds, uint32_t axis) {
        switch (axis) {
        case 0u:
            return (std::max)(0.0f, bounds.max.x - bounds.min.x);
        case 1u:
            return (std::max)(0.0f, bounds.max.y - bounds.min.y);
        default:
            return (std::max)(0.0f, bounds.max.z - bounds.min.z);
        }
    }

    uint32_t LongestBoundsAxis(const Bounds& bounds) {
        const float extentX = BoundsExtentOnAxis(bounds, 0u);
        const float extentY = BoundsExtentOnAxis(bounds, 1u);
        const float extentZ = BoundsExtentOnAxis(bounds, 2u);
        if (extentX >= extentY && extentX >= extentZ) {
            return 0u;
        }
        return extentY >= extentZ ? 1u : 2u;
    }

    float MaxBoundsExtent(const Bounds& bounds) {
        return (std::max)(
            BoundsExtentOnAxis(bounds, 0u),
            (std::max)(BoundsExtentOnAxis(bounds, 1u), BoundsExtentOnAxis(bounds, 2u)));
    }

    MATH::Vec3 BoundsCenter(const Bounds& bounds) {
        return {
            (bounds.min.x + bounds.max.x) * 0.5f,
            (bounds.min.y + bounds.max.y) * 0.5f,
            (bounds.min.z + bounds.max.z) * 0.5f,
        };
    }

    float BoundsRadius(const Bounds& bounds) {
        const MATH::Vec3 center = BoundsCenter(bounds);
        const MATH::Vec3 extent{
            (std::max)(bounds.max.x - center.x, 0.0f),
            (std::max)(bounds.max.y - center.y, 0.0f),
            (std::max)(bounds.max.z - center.z, 0.0f),
        };
        return std::sqrt(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z);
    }

    Bounds BuildCenterRadiusBounds(const MATH::Vec3& center, float radius) {
        const float safeRadius = (std::max)(radius, 0.0f);
        Bounds bounds{};
        bounds.min = {
            center.x - safeRadius,
            center.y - safeRadius,
            center.z - safeRadius,
        };
        bounds.max = {
            center.x + safeRadius,
            center.y + safeRadius,
            center.z + safeRadius,
        };
        return bounds;
    }

    float ResolveSectionLodErrorBudgetNdc(const ClusteredGeometryCookSettings& settings) {
        float budget = settings.lod1TargetError;
        if (settings.surfacePartitionPolicy == SurfacePartitionPolicy::SceneStatic) {
            budget = (std::max)(budget, settings.lod2TargetError * 0.80f);
        } else if (settings.surfacePartitionPolicy == SurfacePartitionPolicy::CharacterStatic) {
            budget = (std::max)(budget, settings.lod2TargetError * 0.75f);
        }

        // 後段 LOD は section 単位で選ばれるため、LOD1 だけの誤差予算だと
        // LOD3/4 が遠距離まで選ばれにくい。近景は screen radius で守る。
        return (std::max)(budget, 0.0005f);
    }

    Bounds ResolveSectionLodMetricBounds(
        const ClusteredGeometryCookSettings& settings,
        const Bounds& sectionBounds,
        const Bounds& surfaceBounds,
        const Bounds& assetBounds,
        size_t assetSurfaceCount,
        bool partitioned) {

        if (!BOUNDS::IsUsable(sectionBounds)) {
            return sectionBounds;
        }

        const float sectionRadius = BoundsRadius(sectionBounds);
        if (sectionRadius <= 0.000001f) {
            return sectionBounds;
        }

        if (partitioned) {
            if (settings.surfacePartitionPolicy != SurfacePartitionPolicy::SceneStatic) {
                return sectionBounds;
            }

            // Scene の平面 section は見た目の対角線で LOD 判定が引っ張られやすい。
            // 可視判定用 bounds はそのまま使い、LOD metric だけを cook 時の目標粒度へ寄せる。
            const float targetRadius =
                (std::max)(0.35f, settings.largeSurfacePartitionMaxExtent * 0.50f);
            const float metricRadius = (std::min)(sectionRadius, targetRadius);
            if (metricRadius >= sectionRadius * 0.98f) {
                return sectionBounds;
            }
            return BuildCenterRadiusBounds(BoundsCenter(sectionBounds), metricRadius);
        }

        float referenceRadius = BOUNDS::IsUsable(surfaceBounds)
            ? BoundsRadius(surfaceBounds)
            : sectionRadius;

        // Character は部位ごとの LOD 判定を優先するため、asset 全体半径で section を膨らませない。
        const float compactAssetScale =
            settings.surfacePartitionPolicy == SurfacePartitionPolicy::CharacterStatic
                ? 0.0f
                : (settings.surfacePartitionPolicy == SurfacePartitionPolicy::SceneStatic ? 0.15f : 0.30f);
        if (compactAssetScale > 0.0f && assetSurfaceCount <= 16u && BOUNDS::IsUsable(assetBounds)) {
            const float assetRadius = BoundsRadius(assetBounds);
            referenceRadius = (std::max)(referenceRadius, assetRadius * compactAssetScale);
        }

        const float sectionMetricScale =
            settings.surfacePartitionPolicy == SurfacePartitionPolicy::CharacterStatic ? 0.20f : 0.35f;
        const float minMetricRadius = referenceRadius * sectionMetricScale;
        const float metricRadius = (std::max)(sectionRadius, minMetricRadius);
        if (metricRadius <= sectionRadius * 1.01f) {
            return sectionBounds;
        }

        return BuildCenterRadiusBounds(BoundsCenter(sectionBounds), metricRadius);
    }

    void ConfigureSectionLodMetric(
        ClusterSurfaceSection& section,
        const ClusteredGeometryCookSettings& settings,
        const Bounds& metricBounds) {

        section.lodMetricBounds = BOUNDS::IsUsable(metricBounds)
            ? metricBounds
            : section.localBounds;
        section.lodErrorBudgetNdc = ResolveSectionLodErrorBudgetNdc(settings);
    }

    void FinalizeAssetLodMetrics(
        ClusteredGeometryAsset& asset,
        const ClusteredGeometryCookSettings& settings) {

        for (ClusterSurfaceSection& section : asset.surfaceSections) {
            const ClusterSurface* surface = nullptr;
            if (section.surfaceIndex < asset.surfaces.size()) {
                surface = &asset.surfaces[section.surfaceIndex];
            }

            const Bounds& surfaceBounds = surface != nullptr
                ? surface->localBounds
                : section.localBounds;
            const bool partitioned = surface != nullptr && surface->sectionCount > 1u;

            ConfigureSectionLodMetric(
                section,
                settings,
                ResolveSectionLodMetricBounds(
                    settings,
                    section.localBounds,
                    surfaceBounds,
                    asset.localBounds,
                    asset.surfaces.size(),
                    partitioned));
        }
    }

    float TriangleCenterOnAxis(const SourceTriangle& tri, uint32_t axis) {
        switch (axis) {
        case 0u:
            return (tri.bounds.min.x + tri.bounds.max.x) * 0.5f;
        case 1u:
            return (tri.bounds.min.y + tri.bounds.max.y) * 0.5f;
        default:
            return (tri.bounds.min.z + tri.bounds.max.z) * 0.5f;
        }
    }

    uint32_t TriangleNormalBucket(const SourceTriangle& tri) {
        const float ax = std::abs(tri.normal.x);
        const float ay = std::abs(tri.normal.y);
        const float az = std::abs(tri.normal.z);
        if (ax >= ay && ax >= az) {
            return tri.normal.x >= 0.0f ? 0u : 1u;
        }
        if (ay >= az) {
            return tri.normal.y >= 0.0f ? 2u : 3u;
        }
        return tri.normal.z >= 0.0f ? 4u : 5u;
    }

    SurfaceShapeAnalysis AnalyzeSurfaceShape(
        const std::vector<SourceTriangle>& triangles,
        const Bounds& bounds) {

        SurfaceShapeAnalysis analysis{};
        if (triangles.empty() || !BOUNDS::IsUsable(bounds)) {
            return analysis;
        }

        std::array<float, 3u> extents{
            BoundsExtentOnAxis(bounds, 0u),
            BoundsExtentOnAxis(bounds, 1u),
            BoundsExtentOnAxis(bounds, 2u),
        };
        std::sort(extents.begin(), extents.end());
        analysis.minExtent = extents[0];
        analysis.midExtent = extents[1];
        analysis.maxExtent = extents[2];

        double totalArea = 0.0;
        double bucketAreaMax = 0.0;
        std::array<double, 6u> bucketAreas{};
        MATH::Vec3 normalSum{};
        for (const SourceTriangle& tri : triangles) {
            const float weight = std::isfinite(tri.area) && tri.area > 0.0f
                ? tri.area
                : 1.0f;
            totalArea += static_cast<double>(weight);
            normalSum = normalSum + tri.normal * weight;
            bucketAreas[TriangleNormalBucket(tri)] += static_cast<double>(weight);
        }

        for (double bucketArea : bucketAreas) {
            bucketAreaMax = (std::max)(bucketAreaMax, bucketArea);
        }

        if (totalArea > 0.0) {
            analysis.normalCoherence =
                static_cast<float>(MATH::Length(normalSum) / totalArea);
            analysis.dominantNormalRatio =
                static_cast<float>(bucketAreaMax / totalArea);
        }

        const bool thinBounds =
            analysis.maxExtent > 0.0001f &&
            analysis.midExtent > 0.0001f &&
            analysis.minExtent <= (std::max)(0.08f, analysis.maxExtent * 0.04f);
        analysis.coherentPlanar =
            analysis.normalCoherence >= 0.88f &&
            analysis.dominantNormalRatio >= 0.82f;
        analysis.largePlanar =
            analysis.coherentPlanar &&
            thinBounds &&
            analysis.maxExtent >= 1.5f;
        return analysis;
    }

    TrianglePartitionConfig ResolveTrianglePartitionConfig(
        const ClusteredGeometryCookSettings& settings,
        const SurfaceShapeAnalysis& analysis) {

        TrianglePartitionConfig config{};
        config.minChunkTriangles =
            (std::max)(1u, settings.largeSurfacePartitionMinTrianglesPerChunk);
        const uint32_t minChunkByClusterEstimate =
            (std::max)(1u, settings.maxTrianglesPerCluster) *
            (std::max)(1u, settings.minPartitionClusterEstimate);
        config.minChunkTriangles =
            (std::max)(config.minChunkTriangles, minChunkByClusterEstimate);
        config.maxDepth = (std::max)(1u, settings.largeSurfacePartitionMaxDepth);
        config.maxExtent =
            (std::max)(0.25f, settings.largeSurfacePartitionMaxExtent);

        if (!settings.balancePlanarStaticSurfaces ||
            settings.surfacePartitionPolicy != SurfacePartitionPolicy::SceneStatic) {
            return config;
        }

        if (analysis.largePlanar) {
            config.planarCoarsened = true;
            config.maxExtent = (std::max)(
                config.maxExtent,
                (std::max)(settings.planarStaticSurfaceMinPartitionExtent, 3.0f));
            config.minChunkTriangles = (std::max)(
                config.minChunkTriangles,
                (std::max)(
                    settings.planarStaticSurfaceMinTrianglesPerChunk,
                    settings.maxTrianglesPerCluster * 12u));
            config.maxDepth = (std::min)(
                config.maxDepth,
                (std::max)(1u, settings.planarStaticSurfaceMaxDepth));
        } else if (analysis.coherentPlanar) {
            config.maxExtent = (std::max)(config.maxExtent, 1.5f);
            config.minChunkTriangles = (std::max)(
                config.minChunkTriangles,
                settings.maxTrianglesPerCluster * 8u);
            config.maxDepth = (std::min)(config.maxDepth, 5u);
        } else {
            config.maxExtent = (std::max)(config.maxExtent, 0.75f);
            config.minChunkTriangles = (std::max)(
                config.minChunkTriangles,
                settings.maxTrianglesPerCluster * 4u);
        }

        return config;
    }

    Bounds ComputeTriangleSubsetBounds(
        const std::vector<SourceTriangle>& triangles,
        const std::vector<uint32_t>& triangleIndices) {

        Bounds bounds{};
        bool hasBounds = false;
        for (uint32_t triangleIndex : triangleIndices) {
            if (triangleIndex >= triangles.size()) {
                continue;
            }
            const Bounds& triBounds = triangles[triangleIndex].bounds;
            if (!BOUNDS::IsUsable(triBounds)) {
                continue;
            }
            bounds = hasBounds ? MergeBounds(bounds, triBounds) : triBounds;
            hasBounds = true;
        }
        return hasBounds ? bounds : Bounds{};
    }

    bool ShouldPartitionLargeStaticSurface(
        const SurfaceCookInput& work,
        const Bounds& bounds,
        const ClusteredGeometryCookSettings& settings) {

        if (!settings.partitionLargeStaticSurfaces ||
            settings.surfacePartitionPolicy == SurfacePartitionPolicy::Disabled ||
            !ShouldUsePermissiveOpaqueLods(work.flags) ||
            CountSurfaceTriangles(work) < settings.largeSurfacePartitionMinTriangles ||
            !BOUNDS::IsUsable(bounds)) {
            return false;
        }

        const float maxExtent =
            (std::max)(0.25f, settings.largeSurfacePartitionMaxExtent);
        return MaxBoundsExtent(bounds) > maxExtent;
    }

    void PartitionTriangleIndicesRecursive(
        const std::vector<SourceTriangle>& triangles,
        std::vector<uint32_t> triangleIndices,
        const TrianglePartitionConfig& config,
        uint32_t depth,
        std::vector<std::vector<uint32_t>>& outPartitions);

    void AppendSpatialTrianglePartitions(
        const std::vector<SourceTriangle>& triangles,
        std::vector<uint32_t> triangleIndices,
        const TrianglePartitionConfig& config,
        std::vector<std::vector<uint32_t>>& outPartitions) {

        if (triangleIndices.empty()) {
            return;
        }
        PartitionTriangleIndicesRecursive(
            triangles,
            std::move(triangleIndices),
            config,
            0u,
            outPartitions);
    }

    bool BuildNormalAwareTrianglePartitions(
        const std::vector<SourceTriangle>& triangles,
        const ClusteredGeometryCookSettings& settings,
        const TrianglePartitionConfig& config,
        std::vector<std::vector<uint32_t>>& outPartitions) {

        outPartitions.clear();
        if (!settings.buildNormalCone ||
            settings.surfacePartitionPolicy != SurfacePartitionPolicy::SceneStatic ||
            triangles.empty()) {
            return false;
        }

        const uint32_t minChunkTriangles = config.minChunkTriangles;
        std::array<std::vector<uint32_t>, 6u> buckets{};
        for (uint32_t triangleIndex = 0; triangleIndex < triangles.size(); ++triangleIndex) {
            buckets[TriangleNormalBucket(triangles[triangleIndex])].push_back(triangleIndex);
        }

        size_t usableBucketCount = 0;
        for (const std::vector<uint32_t>& bucket : buckets) {
            if (bucket.size() >= minChunkTriangles) {
                ++usableBucketCount;
            }
        }
        if (usableBucketCount <= 1u) {
            return false;
        }

        std::vector<uint32_t> smallBuckets{};
        for (std::vector<uint32_t>& bucket : buckets) {
            if (bucket.empty()) {
                continue;
            }
            if (bucket.size() < minChunkTriangles) {
                smallBuckets.insert(smallBuckets.end(), bucket.begin(), bucket.end());
                continue;
            }
            AppendSpatialTrianglePartitions(
                triangles,
                std::move(bucket),
                config,
                outPartitions);
        }

        if (smallBuckets.size() >= minChunkTriangles) {
            AppendSpatialTrianglePartitions(
                triangles,
                std::move(smallBuckets),
                config,
                outPartitions);
        } else if (!smallBuckets.empty() && !outPartitions.empty()) {
            outPartitions.back().insert(
                outPartitions.back().end(),
                smallBuckets.begin(),
                smallBuckets.end());
        }

        if (outPartitions.size() <= 1u) {
            outPartitions.clear();
            return false;
        }
        return true;
    }

    bool ShouldAcceptTrianglePartitions(
        const std::vector<std::vector<uint32_t>>& partitions,
        const TrianglePartitionConfig& config,
        const ClusteredGeometryCookSettings& settings) {

        if (partitions.size() <= 1u) {
            return false;
        }

        const uint32_t minChunkTriangles = (std::max)(1u, config.minChunkTriangles);
        const uint32_t softMinChunkTriangles = (std::max)(
            minChunkTriangles / 2u,
            (std::max)(1u, settings.maxTrianglesPerCluster) *
            (std::max)(1u, settings.minPartitionClusterEstimate / 2u));

        uint32_t smallPartitionCount = 0;
        uint64_t totalTriangleCount = 0;
        for (const std::vector<uint32_t>& partition : partitions) {
            if (partition.empty()) {
                ++smallPartitionCount;
                continue;
            }
            totalTriangleCount += static_cast<uint64_t>(partition.size());
            if (partition.size() < softMinChunkTriangles) {
                ++smallPartitionCount;
            }
        }

        const double averagePartitionTriangles =
            static_cast<double>(totalTriangleCount) /
            static_cast<double>(partitions.size());
        return averagePartitionTriangles >= static_cast<double>(softMinChunkTriangles) &&
            smallPartitionCount * 4u <= static_cast<uint32_t>(partitions.size());
    }

    void PartitionTriangleIndicesRecursive(
        const std::vector<SourceTriangle>& triangles,
        std::vector<uint32_t> triangleIndices,
        const TrianglePartitionConfig& config,
        uint32_t depth,
        std::vector<std::vector<uint32_t>>& outPartitions) {

        const Bounds bounds = ComputeTriangleSubsetBounds(triangles, triangleIndices);
        const uint32_t minChunkTriangles = config.minChunkTriangles;
        const float maxExtent = config.maxExtent;
        if (triangleIndices.size() < static_cast<size_t>(minChunkTriangles) * 2u ||
            depth >= config.maxDepth ||
            !BOUNDS::IsUsable(bounds) ||
            MaxBoundsExtent(bounds) <= maxExtent) {
            outPartitions.push_back(std::move(triangleIndices));
            return;
        }

        const uint32_t axis = LongestBoundsAxis(bounds);
        auto middle = triangleIndices.begin() +
            static_cast<std::ptrdiff_t>(triangleIndices.size() / 2u);
        std::nth_element(
            triangleIndices.begin(),
            middle,
            triangleIndices.end(),
            [&](uint32_t lhs, uint32_t rhs) {
                const float lhsCenter =
                    TriangleCenterOnAxis(triangles[lhs], axis);
                const float rhsCenter =
                    TriangleCenterOnAxis(triangles[rhs], axis);
                if (lhsCenter == rhsCenter) {
                    return lhs < rhs;
                }
                return lhsCenter < rhsCenter;
            });

        std::vector<uint32_t> left(triangleIndices.begin(), middle);
        std::vector<uint32_t> right(middle, triangleIndices.end());
        if (left.size() < minChunkTriangles || right.size() < minChunkTriangles) {
            outPartitions.push_back(std::move(triangleIndices));
            return;
        }

        PartitionTriangleIndicesRecursive(
            triangles,
            std::move(left),
            config,
            depth + 1u,
            outPartitions);
        PartitionTriangleIndicesRecursive(
            triangles,
            std::move(right),
            config,
            depth + 1u,
            outPartitions);
    }

    bool BuildLargeStaticSurfaceSectionPlans(
        const SurfaceCookInput& work,
        const std::vector<SourceTriangle>& triangles,
        const ClusteredGeometryCookSettings& settings,
        ClusteredGeometryBuildReport& report,
        std::vector<SurfaceSectionPlan>& outSections) {

        outSections.clear();
        const Bounds sourceBounds = ComputeVertexBounds(work.vertices);
        if (!ShouldPartitionLargeStaticSurface(work, sourceBounds, settings)) {
            return false;
        }

        if (triangles.size() < settings.largeSurfacePartitionMinTriangles) {
            return false;
        }

        std::vector<uint32_t> triangleIndices(triangles.size());
        std::iota(triangleIndices.begin(), triangleIndices.end(), 0u);

        const SurfaceShapeAnalysis shapeAnalysis =
            AnalyzeSurfaceShape(triangles, sourceBounds);
        const TrianglePartitionConfig partitionConfig =
            ResolveTrianglePartitionConfig(settings, shapeAnalysis);

        std::vector<std::vector<uint32_t>> partitions{};
        const bool normalPartitioned =
            BuildNormalAwareTrianglePartitions(
                triangles,
                settings,
                partitionConfig,
                partitions);
        if (!normalPartitioned) {
            PartitionTriangleIndicesRecursive(
                triangles,
                std::move(triangleIndices),
                partitionConfig,
                0u,
                partitions);
        }
        if (partitions.size() <= 1u) {
            return false;
        }
        if (!ShouldAcceptTrianglePartitions(partitions, partitionConfig, settings)) {
            ++report.rejectedPartitionedSurfaceCount;
            return false;
        }

        // SurfaceGpuScene は primitive 単位で 1 つの surface を参照するため、
        // cook では runtime surface を分割せず、cluster/page の粒度だけを分割境界に寄せる。
        for (const std::vector<uint32_t>& partition : partitions) {
            if (partition.empty()) {
                continue;
            }

            std::vector<SourceTriangle> partitionTriangles{};
            partitionTriangles.reserve(partition.size());
            for (uint32_t triangleIndex : partition) {
                if (triangleIndex < triangles.size()) {
                    partitionTriangles.push_back(triangles[triangleIndex]);
                }
            }
            if (partitionTriangles.empty()) {
                continue;
            }

            const std::vector<std::vector<uint32_t>> partitionGroups =
                BuildClusterTriangleGroups(work.vertices, partitionTriangles, settings, &report);
            SurfaceSectionPlan section{};
            section.triangleIndices = partition;
            for (const std::vector<uint32_t>& localGroup : partitionGroups) {
                std::vector<uint32_t> group{};
                group.reserve(localGroup.size());
                for (uint32_t localTriangleIndex : localGroup) {
                    if (localTriangleIndex < partition.size()) {
                        group.push_back(partition[localTriangleIndex]);
                    }
                }
                if (!group.empty()) {
                    section.groups.push_back(std::move(group));
                }
            }
            if (!section.groups.empty()) {
                outSections.push_back(std::move(section));
            }
        }
        if (outSections.empty()) {
            return false;
        }

        ++report.partitionedSurfaceCount;
        report.partitionedSurfaceChunkCount += static_cast<uint32_t>(outSections.size());
        if (normalPartitioned) {
            ++report.normalPartitionedSurfaceCount;
            report.normalPartitionedChunkCount += static_cast<uint32_t>(outSections.size());
        }
        if (shapeAnalysis.coherentPlanar) {
            ++report.planarPartitionedSurfaceCount;
            report.planarPartitionedChunkCount += static_cast<uint32_t>(outSections.size());
        }
        if (partitionConfig.planarCoarsened) {
            ++report.planarPartitionCoarsenedSurfaceCount;
        }
        return true;
    }

    SurfaceCookInput BuildSectionCookInput(
        const SurfaceCookInput& source,
        const std::vector<SourceTriangle>& triangles,
        const std::vector<uint32_t>& triangleIndices) {

        SurfaceCookInput sectionWork{};
        sectionWork.nodeIndex = source.nodeIndex;
        sectionWork.meshIndex = source.meshIndex;
        sectionWork.primitiveIndex = source.primitiveIndex;
        sectionWork.materialIndex = source.materialIndex;
        sectionWork.flags = source.flags;

        std::unordered_map<uint32_t, uint32_t> vertexRemap{};
        vertexRemap.reserve(triangleIndices.size() * 3u);
        sectionWork.indices.reserve(triangleIndices.size() * 3u);

        for (uint32_t triangleIndex : triangleIndices) {
            if (triangleIndex >= triangles.size()) {
                continue;
            }

            const SourceTriangle& tri = triangles[triangleIndex];
            const uint32_t sourceIndices[3] = { tri.i0, tri.i1, tri.i2 };
            for (uint32_t sourceIndex : sourceIndices) {
                if (sourceIndex >= source.vertices.size()) {
                    continue;
                }

                auto it = vertexRemap.find(sourceIndex);
                if (it == vertexRemap.end()) {
                    const uint32_t remappedIndex =
                        static_cast<uint32_t>(sectionWork.vertices.size());
                    vertexRemap[sourceIndex] = remappedIndex;
                    sectionWork.vertices.push_back(source.vertices[sourceIndex]);
                    sectionWork.indices.push_back(remappedIndex);
                } else {
                    sectionWork.indices.push_back(it->second);
                }
            }
        }

        return sectionWork;
    }


} // namespace HIKARI::ASSETS::GEOMETRY::COOKING
