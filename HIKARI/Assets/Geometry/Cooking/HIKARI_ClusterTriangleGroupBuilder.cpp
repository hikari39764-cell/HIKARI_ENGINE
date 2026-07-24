#include "Assets/Geometry/Cooking/Internal/HIKARI_ClusteredGeometryCookInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "../../../../ThirdParty/meshoptimizer/src/meshoptimizer.h"

namespace HIKARI::ASSETS::GEOMETRY::COOKING {

    struct TriangleKey {
        uint32_t i0 = 0;
        uint32_t i1 = 0;
        uint32_t i2 = 0;

        bool operator==(const TriangleKey& rhs) const {
            return i0 == rhs.i0 && i1 == rhs.i1 && i2 == rhs.i2;
        }
    };

    struct TriangleKeyHash {
        size_t operator()(const TriangleKey& key) const {
            uint64_t h = 1469598103934665603ull;
            const uint32_t values[3] = { key.i0, key.i1, key.i2 };
            for (uint32_t value : values) {
                h ^= static_cast<uint64_t>(value);
                h *= 1099511628211ull;
            }
            return static_cast<size_t>(h);
        }
    };

    TriangleKey MakeTriangleKey(const SourceTriangle& tri) {
        return { tri.i0, tri.i1, tri.i2 };
    }

    struct TriangleGroupStats {
        uint32_t groupCount = 0;
        uint32_t totalTriangleCount = 0;
        uint32_t lowTriangleGroupCount = 0;
        uint32_t normalCoherentGroupCount = 0;
        float averageTrianglesPerGroup = 0.0f;
        float normalCoherentGroupRatio = 0.0f;
    };

    uint32_t ResolvePreferredClusterTriangleCount(const ClusteredGeometryCookSettings& settings) {
        const uint32_t maxTriangles = (std::max)(1u, settings.maxTrianglesPerCluster);
        const float occupancyRatio =
            (std::max)(0.25f, (std::min)(settings.minClusterOccupancyRatio, 1.0f));
        const uint32_t occupancyTarget =
            static_cast<uint32_t>(std::ceil(static_cast<float>(maxTriangles) * occupancyRatio));
        return (std::min)(
            maxTriangles,
            (std::max)((std::max)(1u, settings.minTrianglesPerCluster), occupancyTarget));
    }

    float ResolveNormalDotThreshold(float value) {
        return (std::max)(-1.0f, (std::min)(value, 0.99f));
    }

    float ResolvePositiveTriangleArea(const SourceTriangle& tri) {
        return std::isfinite(tri.area) && tri.area > 1e-7f
            ? tri.area
            : 1.0f;
    }

    struct TriangleGroupNormalBasis {
        bool hasNormals = false;
        bool coherent = false;
        MATH::Vec3 axis{ 0.0f, 1.0f, 0.0f };
        float weight = 0.0f;
    };

    TriangleGroupNormalBasis ResolveTriangleGroupNormalBasis(
        const std::vector<SourceTriangle>& triangles,
        const std::vector<uint32_t>& group) {

        TriangleGroupNormalBasis basis{};
        MATH::Vec3 normalSum{};
        for (uint32_t triangleIndex : group) {
            if (triangleIndex >= triangles.size()) {
                continue;
            }

            const SourceTriangle& tri = triangles[triangleIndex];
            if (MATH::Length(tri.normal) <= 1e-5f) {
                continue;
            }

            const float weight = ResolvePositiveTriangleArea(tri);
            normalSum = normalSum + tri.normal * weight;
            basis.weight += weight;
            basis.hasNormals = true;
        }

        if (!basis.hasNormals) {
            return basis;
        }

        if (MATH::Length(normalSum) > 1e-5f) {
            basis.axis = MATH::Normalize(normalSum);
            basis.coherent = true;
        }
        return basis;
    }

    float TriangleGroupMinNormalDotAgainstAxis(
        const std::vector<SourceTriangle>& triangles,
        const std::vector<uint32_t>& group,
        const MATH::Vec3& axis) {

        float minDot = 1.0f;
        bool hasNormal = false;
        for (uint32_t triangleIndex : group) {
            if (triangleIndex >= triangles.size()) {
                continue;
            }

            const SourceTriangle& tri = triangles[triangleIndex];
            if (MATH::Length(tri.normal) <= 1e-5f) {
                continue;
            }

            minDot = (std::min)(minDot, MATH::Dot(axis, tri.normal));
            hasNormal = true;
        }
        return hasNormal ? minDot : 1.0f;
    }

    float TriangleGroupMinNormalDot(
        const std::vector<SourceTriangle>& triangles,
        const std::vector<uint32_t>& group) {

        const TriangleGroupNormalBasis basis =
            ResolveTriangleGroupNormalBasis(triangles, group);
        if (!basis.hasNormals) {
            return 1.0f;
        }
        if (!basis.coherent) {
            return -1.0f;
        }
        return TriangleGroupMinNormalDotAgainstAxis(triangles, group, basis.axis);
    }

    bool AreTriangleGroupsNormalCompatible(
        const std::vector<SourceTriangle>& triangles,
        const std::vector<uint32_t>& lhs,
        const std::vector<uint32_t>& rhs,
        const ClusteredGeometryCookSettings& settings) {

        const float minDot =
            ResolveNormalDotThreshold(settings.clusterMergeNormalMinDot);
        if (minDot <= -0.99f) {
            return true;
        }

        const TriangleGroupNormalBasis lhsBasis =
            ResolveTriangleGroupNormalBasis(triangles, lhs);
        const TriangleGroupNormalBasis rhsBasis =
            ResolveTriangleGroupNormalBasis(triangles, rhs);
        if (!lhsBasis.hasNormals || !rhsBasis.hasNormals) {
            return true;
        }
        if (!lhsBasis.coherent || !rhsBasis.coherent) {
            return false;
        }
        if (MATH::Dot(lhsBasis.axis, rhsBasis.axis) < minDot) {
            return false;
        }

        const MATH::Vec3 mergedNormal =
            lhsBasis.axis * lhsBasis.weight + rhsBasis.axis * rhsBasis.weight;
        if (MATH::Length(mergedNormal) <= 1e-5f) {
            return false;
        }

        const MATH::Vec3 mergedAxis = MATH::Normalize(mergedNormal);
        return TriangleGroupMinNormalDotAgainstAxis(triangles, lhs, mergedAxis) >= minDot &&
            TriangleGroupMinNormalDotAgainstAxis(triangles, rhs, mergedAxis) >= minDot;
    }

    void AppendUniqueTriangleVertices(
        const SourceTriangle& tri,
        std::vector<uint32_t>& vertices) {

        const uint32_t ids[3] = { tri.i0, tri.i1, tri.i2 };
        for (uint32_t id : ids) {
            if (std::find(vertices.begin(), vertices.end(), id) == vertices.end()) {
                vertices.push_back(id);
            }
        }
    }

    bool CanMergeTriangleGroups(
        const std::vector<SourceTriangle>& triangles,
        const std::vector<uint32_t>& lhs,
        const std::vector<uint32_t>& rhs,
        const ClusteredGeometryCookSettings& settings) {

        if (lhs.size() + rhs.size() >
            static_cast<size_t>((std::max)(1u, settings.maxTrianglesPerCluster))) {
            return false;
        }
        if (!AreTriangleGroupsNormalCompatible(triangles, lhs, rhs, settings)) {
            return false;
        }

        std::vector<uint32_t> vertices{};
        vertices.reserve((lhs.size() + rhs.size()) * 3u);
        for (uint32_t triangleIndex : lhs) {
            if (triangleIndex < triangles.size()) {
                AppendUniqueTriangleVertices(triangles[triangleIndex], vertices);
            }
        }
        for (uint32_t triangleIndex : rhs) {
            if (triangleIndex < triangles.size()) {
                AppendUniqueTriangleVertices(triangles[triangleIndex], vertices);
            }
        }
        return vertices.size() <= static_cast<size_t>((std::max)(3u, settings.maxVerticesPerCluster));
    }

    MATH::Vec3 TriangleGroupCenter(
        const std::vector<SourceTriangle>& triangles,
        const std::vector<uint32_t>& group) {

        const Bounds bounds = ComputeTriangleSubsetBounds(triangles, group);
        if (!BOUNDS::IsUsable(bounds)) {
            return {};
        }
        return (bounds.min + bounds.max) * 0.5f;
    }

    float DistanceSquared(const MATH::Vec3& lhs, const MATH::Vec3& rhs) {
        const MATH::Vec3 delta = lhs - rhs;
        return MATH::Dot(delta, delta);
    }

    TriangleGroupStats EvaluateTriangleGroups(
        const std::vector<SourceTriangle>& triangles,
        const std::vector<std::vector<uint32_t>>& groups,
        uint32_t preferredTrianglesPerGroup,
        const ClusteredGeometryCookSettings& settings) {

        TriangleGroupStats stats{};
        stats.groupCount = static_cast<uint32_t>(groups.size());
        const float coherentNormalMinDot =
            ResolveNormalDotThreshold(settings.normalBucketCoherentGroupMinDot);
        for (const std::vector<uint32_t>& group : groups) {
            stats.totalTriangleCount += static_cast<uint32_t>(group.size());
            if (group.size() < preferredTrianglesPerGroup) {
                ++stats.lowTriangleGroupCount;
            }
            if (TriangleGroupMinNormalDot(triangles, group) >= coherentNormalMinDot) {
                ++stats.normalCoherentGroupCount;
            }
        }
        if (stats.groupCount > 0u) {
            stats.averageTrianglesPerGroup =
                static_cast<float>(
                    static_cast<double>(stats.totalTriangleCount) /
                    static_cast<double>(stats.groupCount));
            stats.normalCoherentGroupRatio =
                static_cast<float>(
                    static_cast<double>(stats.normalCoherentGroupCount) /
                    static_cast<double>(stats.groupCount));
        }
        return stats;
    }

    std::vector<std::vector<uint32_t>> CompactUnderfilledClusterGroups(
        const std::vector<SourceTriangle>& triangles,
        std::vector<std::vector<uint32_t>> groups,
        const ClusteredGeometryCookSettings& settings,
        ClusteredGeometryBuildReport* report) {

        if (!settings.compactUnderfilledClusterGroups || groups.size() <= 1u) {
            return groups;
        }

        const uint32_t preferredTriangles = ResolvePreferredClusterTriangleCount(settings);
        std::vector<bool> consumed(groups.size(), false);
        std::vector<std::vector<uint32_t>> compacted{};
        compacted.reserve(groups.size());

        for (size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
            if (consumed[groupIndex]) {
                continue;
            }

            std::vector<uint32_t> current = std::move(groups[groupIndex]);
            consumed[groupIndex] = true;
            while (current.size() < preferredTriangles) {
                const MATH::Vec3 currentCenter = TriangleGroupCenter(triangles, current);
                size_t bestIndex = groups.size();
                float bestScore = (std::numeric_limits<float>::max)();
                for (size_t candidateIndex = groupIndex + 1u;
                     candidateIndex < groups.size();
                     ++candidateIndex) {
                    if (consumed[candidateIndex] || groups[candidateIndex].empty()) {
                        continue;
                    }
                    if (!CanMergeTriangleGroups(
                            triangles,
                            current,
                            groups[candidateIndex],
                            settings)) {
                        continue;
                    }

                    const float score = DistanceSquared(
                        currentCenter,
                        TriangleGroupCenter(triangles, groups[candidateIndex]));
                    if (score < bestScore ||
                        (score == bestScore &&
                         groups[candidateIndex].size() >
                            (bestIndex < groups.size() ? groups[bestIndex].size() : 0u))) {
                        bestScore = score;
                        bestIndex = candidateIndex;
                    }
                }

                if (bestIndex >= groups.size()) {
                    break;
                }

                current.insert(
                    current.end(),
                    groups[bestIndex].begin(),
                    groups[bestIndex].end());
                consumed[bestIndex] = true;
                if (report != nullptr) {
                    ++report->mergedClusterGroupCount;
                }
            }

            if (!current.empty() &&
                current.size() < preferredTriangles &&
                !compacted.empty() &&
                CanMergeTriangleGroups(triangles, compacted.back(), current, settings)) {
                compacted.back().insert(
                    compacted.back().end(),
                    current.begin(),
                    current.end());
                if (report != nullptr) {
                    ++report->mergedClusterGroupCount;
                }
                continue;
            }

            if (!current.empty()) {
                compacted.push_back(std::move(current));
            }
        }

        if (report != nullptr && compacted.size() < groups.size()) {
            report->compactedClusterGroupCount +=
                static_cast<uint32_t>(groups.size() - compacted.size());
        }
        return compacted;
    }

    std::vector<std::vector<uint32_t>> BuildSequentialTriangleGroups(
        const std::vector<SourceTriangle>& triangles,
        const ClusteredGeometryCookSettings& settings,
        ClusteredGeometryBuildReport* report = nullptr) {

        std::vector<std::vector<uint32_t>> groups{};
        const uint32_t maxTriangles =
            (std::max)(1u, settings.maxTrianglesPerCluster);
        const uint32_t maxVertices =
            (std::max)(3u, settings.maxVerticesPerCluster);
        auto countNewVertices = [](const SourceTriangle& tri, const std::vector<uint32_t>& vertices) {
            uint32_t added = 0;
            const uint32_t ids[3] = { tri.i0, tri.i1, tri.i2 };
            for (uint32_t id : ids) {
                if (std::find(vertices.begin(), vertices.end(), id) == vertices.end()) {
                    ++added;
                }
            }
            return added;
        };
        auto appendTriangleVertices = [](const SourceTriangle& tri, std::vector<uint32_t>& vertices) {
            const uint32_t ids[3] = { tri.i0, tri.i1, tri.i2 };
            for (uint32_t id : ids) {
                if (std::find(vertices.begin(), vertices.end(), id) == vertices.end()) {
                    vertices.push_back(id);
                }
            }
        };
        for (uint32_t i = 0; i < triangles.size();) {
            std::vector<uint32_t> group{};
            group.reserve(maxTriangles);
            std::vector<uint32_t> groupVertices{};
            groupVertices.reserve(maxVertices);
            for (; i < triangles.size() && group.size() < maxTriangles;) {
                const uint32_t newVertices = countNewVertices(triangles[i], groupVertices);
                if (!group.empty() && groupVertices.size() + newVertices > maxVertices) {
                    break;
                }
                group.push_back(i);
                appendTriangleVertices(triangles[i], groupVertices);
                ++i;
            }
            if (!group.empty()) {
                groups.push_back(std::move(group));
            } else {
                ++i;
            }
        }
        return CompactUnderfilledClusterGroups(
            triangles,
            std::move(groups),
            settings,
            report);
    }

    std::vector<std::vector<uint32_t>> BuildMeshoptTriangleGroups(
        const std::vector<ClusterVertex>& vertices,
        const std::vector<SourceTriangle>& triangles,
        const ClusteredGeometryCookSettings& settings,
        ClusteredGeometryBuildReport* report = nullptr) {

        if (vertices.empty() || triangles.empty()) {
            return {};
        }

        const size_t maxVertices =
            (std::min<size_t>)((std::max)(1u, settings.maxVerticesPerCluster), 256u);
        const size_t maxTriangles =
            (std::min<size_t>)((std::max)(1u, settings.maxTrianglesPerCluster), 512u);
        const size_t minTriangles =
            (std::min<size_t>)(
                (std::max<size_t>)(1u, settings.minTrianglesPerCluster),
                maxTriangles);
        if (maxVertices < 3u || maxTriangles == 0u) {
            return BuildSequentialTriangleGroups(triangles, settings, report);
        }

        std::vector<unsigned int> indices{};
        indices.reserve(triangles.size() * 3u);
        std::unordered_map<TriangleKey, std::vector<uint32_t>, TriangleKeyHash> triangleLookup{};
        triangleLookup.reserve(triangles.size());
        for (uint32_t triangleIndex = 0; triangleIndex < triangles.size(); ++triangleIndex) {
            const SourceTriangle& tri = triangles[triangleIndex];
            if (tri.i0 >= vertices.size() || tri.i1 >= vertices.size() || tri.i2 >= vertices.size()) {
                continue;
            }

            indices.push_back(tri.i0);
            indices.push_back(tri.i1);
            indices.push_back(tri.i2);
            triangleLookup[MakeTriangleKey(tri)].push_back(triangleIndex);
        }
        if (indices.empty()) {
            return {};
        }

        if (indices.size() >= 6u) {
            std::vector<unsigned int> sortedIndices(indices.size());
            meshopt_spatialSortTriangles(
                sortedIndices.data(),
                indices.data(),
                indices.size(),
                &vertices[0].position.x,
                vertices.size(),
                sizeof(ClusterVertex));
            indices = std::move(sortedIndices);
        }

        const size_t meshletBound =
            meshopt_buildMeshletsBound(indices.size(), maxVertices, minTriangles);
        std::vector<meshopt_Meshlet> meshlets(meshletBound);
        std::vector<unsigned int> meshletVertices(indices.size());
        std::vector<unsigned char> meshletTriangles(indices.size());

        // 正式な meshlet builder で cluster を作り、後段の独自 HCMESH レイアウトへ変換する。
        const size_t meshletCount = meshopt_buildMeshletsFlex(
            meshlets.data(),
            meshletVertices.data(),
            meshletTriangles.data(),
            indices.data(),
            indices.size(),
            &vertices[0].position.x,
            vertices.size(),
            sizeof(ClusterVertex),
            maxVertices,
            minTriangles,
            maxTriangles,
            (std::max)(0.0f, settings.meshletConeWeight),
            (std::max)(0.0f, settings.meshletSplitFactor));
        if (meshletCount == 0u) {
            return BuildSequentialTriangleGroups(triangles, settings, report);
        }

        std::vector<std::vector<uint32_t>> groups{};
        groups.reserve(meshletCount);
        for (size_t meshletIndex = 0; meshletIndex < meshletCount; ++meshletIndex) {
            const meshopt_Meshlet& meshlet = meshlets[meshletIndex];
            if (meshlet.triangle_count == 0u || meshlet.vertex_count == 0u) {
                continue;
            }

            meshopt_optimizeMeshlet(
                meshletVertices.data() + meshlet.vertex_offset,
                meshletTriangles.data() + meshlet.triangle_offset,
                meshlet.triangle_count,
                meshlet.vertex_count);

            std::vector<uint32_t> group{};
            group.reserve(meshlet.triangle_count);
            for (uint32_t triOffset = 0; triOffset < meshlet.triangle_count; ++triOffset) {
                const uint32_t base = meshlet.triangle_offset + triOffset * 3u;
                const unsigned char local0 = meshletTriangles[base + 0u];
                const unsigned char local1 = meshletTriangles[base + 1u];
                const unsigned char local2 = meshletTriangles[base + 2u];
                if (local0 >= meshlet.vertex_count ||
                    local1 >= meshlet.vertex_count ||
                    local2 >= meshlet.vertex_count) {
                    continue;
                }

                TriangleKey key{};
                key.i0 = meshletVertices[meshlet.vertex_offset + local0];
                key.i1 = meshletVertices[meshlet.vertex_offset + local1];
                key.i2 = meshletVertices[meshlet.vertex_offset + local2];

                auto it = triangleLookup.find(key);
                if (it == triangleLookup.end() || it->second.empty()) {
                    return BuildSequentialTriangleGroups(triangles, settings, report);
                }

                group.push_back(it->second.back());
                it->second.pop_back();
            }

            if (!group.empty()) {
                groups.push_back(std::move(group));
            }
        }

        return groups.empty()
            ? BuildSequentialTriangleGroups(triangles, settings, report)
            : CompactUnderfilledClusterGroups(
                triangles,
                std::move(groups),
                settings,
                report);
    }

    std::vector<std::vector<uint32_t>> BuildClusterTriangleGroups(
        const std::vector<ClusterVertex>& vertices,
        const std::vector<SourceTriangle>& triangles,
        const ClusteredGeometryCookSettings& settings,
        ClusteredGeometryBuildReport* report) {

        const size_t maxTriangles =
            (std::min<size_t>)((std::max)(1u, settings.maxTrianglesPerCluster), 512u);
        const bool coneFriendlyScene =
            settings.buildNormalCone &&
            settings.surfacePartitionPolicy == SurfacePartitionPolicy::SceneStatic &&
            triangles.size() >= maxTriangles * 2u;
        if (!coneFriendlyScene) {
            return BuildMeshoptTriangleGroups(vertices, triangles, settings, report);
        }

        std::array<std::vector<uint32_t>, 6u> buckets{};
        for (uint32_t triangleIndex = 0; triangleIndex < triangles.size(); ++triangleIndex) {
            buckets[TriangleNormalBucket(triangles[triangleIndex])].push_back(triangleIndex);
        }

        size_t nonEmptyBucketCount = 0;
        for (const std::vector<uint32_t>& bucket : buckets) {
            if (!bucket.empty()) {
                ++nonEmptyBucketCount;
            }
        }
        if (nonEmptyBucketCount <= 1u) {
            return BuildMeshoptTriangleGroups(vertices, triangles, settings, report);
        }

        std::vector<std::vector<uint32_t>> rawGroups =
            BuildMeshoptTriangleGroups(vertices, triangles, settings, nullptr);
        std::vector<std::vector<uint32_t>> groups{};
        for (const std::vector<uint32_t>& bucket : buckets) {
            if (bucket.empty()) {
                continue;
            }

            std::vector<SourceTriangle> bucketTriangles{};
            bucketTriangles.reserve(bucket.size());
            for (uint32_t triangleIndex : bucket) {
                bucketTriangles.push_back(triangles[triangleIndex]);
            }

            std::vector<std::vector<uint32_t>> bucketGroups =
                BuildMeshoptTriangleGroups(vertices, bucketTriangles, settings, nullptr);
            for (std::vector<uint32_t>& bucketGroup : bucketGroups) {
                bool groupValid = true;
                for (uint32_t& localTriangleIndex : bucketGroup) {
                    if (localTriangleIndex >= bucket.size()) {
                        groupValid = false;
                        break;
                    }
                    localTriangleIndex = bucket[localTriangleIndex];
                }
                if (groupValid && !bucketGroup.empty()) {
                    groups.push_back(std::move(bucketGroup));
                }
            }
        }

        if (groups.empty()) {
            return rawGroups;
        }

        const uint32_t preferredTriangles = ResolvePreferredClusterTriangleCount(settings);
        const TriangleGroupStats rawStats =
            EvaluateTriangleGroups(triangles, rawGroups, preferredTriangles, settings);
        const TriangleGroupStats bucketStats =
            EvaluateTriangleGroups(triangles, groups, preferredTriangles, settings);
        const float maxOverhead =
            (std::max)(1.0f, (std::min)(settings.maxNormalBucketClusterOverhead, 2.0f));
        const uint32_t allowedBucketGroups =
            static_cast<uint32_t>(
                std::ceil(static_cast<float>((std::max)(1u, rawStats.groupCount)) * maxOverhead));
        const float minimumBucketAverage =
            rawStats.averageTrianglesPerGroup / maxOverhead;
        const float qualityBonus =
            (std::max)(0.0f, (std::min)(settings.normalBucketQualityBonusRatio, 1.0f));
        const bool bucketNormalQualityBetter =
            bucketStats.normalCoherentGroupRatio >=
            (std::min)(1.0f, rawStats.normalCoherentGroupRatio + qualityBonus);
        const bool rawNormalQualityWeak = rawStats.normalCoherentGroupRatio < 0.90f;
        const bool bucketAverageStillUseful =
            bucketStats.averageTrianglesPerGroup >=
            rawStats.averageTrianglesPerGroup * 0.75f;
        const bool bucketCullingQualityWorthOverhead =
            rawNormalQualityWeak &&
            bucketNormalQualityBetter &&
            bucketStats.normalCoherentGroupRatio >= 0.80f &&
            bucketAverageStillUseful;
        const bool acceptBucketGroups =
            rawStats.groupCount == 0u ||
            (bucketStats.groupCount <= allowedBucketGroups &&
             (bucketStats.averageTrianglesPerGroup >= minimumBucketAverage ||
              bucketCullingQualityWorthOverhead));
        if (report != nullptr) {
            if (acceptBucketGroups) {
                ++report->acceptedNormalBucketGroupCount;
            } else {
                ++report->rejectedNormalBucketGroupCount;
            }
        }
        return acceptBucketGroups ? groups : rawGroups;
    }


} // namespace HIKARI::ASSETS::GEOMETRY::COOKING
