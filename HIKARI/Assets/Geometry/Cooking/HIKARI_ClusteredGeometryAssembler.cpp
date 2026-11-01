#include "Assets/Geometry/Cooking/Internal/HIKARI_ClusteredGeometryCookInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Math/HIKARI_MathValidation.h"
#include "RenderContracts/Model/HIKARI_ModelBounds.h"
#include "../../../../ThirdParty/meshoptimizer/src/meshoptimizer.h"

namespace HIKARI::ASSETS::GEOMETRY::COOKING {

bool ApplyMeshoptMeshletBounds(const ClusteredGeometryAsset& asset, const MeshCluster& cluster,
                               MeshCluster& outCluster) {
    if (cluster.vertexCount == 0u || cluster.primitiveCount == 0u ||
        cluster.firstVertex + cluster.vertexCount > asset.packedVertices.size() ||
        cluster.firstPrimitive + cluster.primitiveCount > asset.meshletPrimitives.size()) {
        return false;
    }

    std::vector<unsigned int> meshletVertices(cluster.vertexCount);
    std::iota(meshletVertices.begin(), meshletVertices.end(), 0u);

    std::vector<unsigned char> meshletTriangles{};
    meshletTriangles.reserve(static_cast<size_t>(cluster.primitiveCount) * 3u);
    for (uint32_t primitiveOffset = 0; primitiveOffset < cluster.primitiveCount; ++primitiveOffset) {
        const MeshletPrimitive& primitive = asset.meshletPrimitives[cluster.firstPrimitive + primitiveOffset];
        if (primitive.i0 >= cluster.vertexCount || primitive.i1 >= cluster.vertexCount ||
            primitive.i2 >= cluster.vertexCount) {
            return false;
        }
        meshletTriangles.push_back(static_cast<unsigned char>(primitive.i0));
        meshletTriangles.push_back(static_cast<unsigned char>(primitive.i1));
        meshletTriangles.push_back(static_cast<unsigned char>(primitive.i2));
    }

    const ClusterVertex* firstVertex = asset.packedVertices.data() + cluster.firstVertex;
    const meshopt_Bounds bounds =
        meshopt_computeMeshletBounds(meshletVertices.data(), meshletTriangles.data(), cluster.primitiveCount,
                                     &firstVertex->position.x, cluster.vertexCount, sizeof(ClusterVertex));

    if (!std::isfinite(bounds.radius) || bounds.radius <= 0.0f) {
        return false;
    }

    outCluster.sphereCenter = {bounds.center[0], bounds.center[1], bounds.center[2]};
    outCluster.sphereRadius = bounds.radius;
    outCluster.coneApex = {bounds.cone_apex[0], bounds.cone_apex[1], bounds.cone_apex[2]};
    outCluster.coneAxis = {bounds.cone_axis[0], bounds.cone_axis[1], bounds.cone_axis[2]};
    outCluster.coneCutoff = bounds.cone_cutoff;
    if (!MATH::IsFinite(outCluster.coneApex) || !MATH::IsFinite(outCluster.coneAxis) ||
        !std::isfinite(outCluster.coneCutoff) || MATH::Length(outCluster.coneAxis) <= 1.0e-5f) {
        outCluster.coneApex = outCluster.sphereCenter;
        outCluster.coneAxis = {0.0f, 1.0f, 0.0f};
        outCluster.coneCutoff = 1.0f;
    }
    return true;
}

uint32_t ExpandMorton10(uint32_t value) {
    value &= 0x000003ffu;
    value = (value | (value << 16u)) & 0x030000ffu;
    value = (value | (value << 8u)) & 0x0300f00fu;
    value = (value | (value << 4u)) & 0x030c30c3u;
    value = (value | (value << 2u)) & 0x09249249u;
    return value;
}

uint32_t EncodeMorton3D(uint32_t x, uint32_t y, uint32_t z) {
    return (ExpandMorton10(x) << 2u) | (ExpandMorton10(y) << 1u) | ExpandMorton10(z);
}

uint32_t QuantizeMortonAxis(float value, float minValue, float extent) {
    if (!std::isfinite(value) || !std::isfinite(minValue) || !std::isfinite(extent) || extent <= 1.0e-5f) {
        return 0u;
    }

    const float normalized = (std::max)(0.0f, (std::min)((value - minValue) / extent, 1.0f));
    return static_cast<uint32_t>(normalized * 1023.0f + 0.5f);
}

uint32_t ClusterMortonCode(const MeshCluster& cluster, const Bounds& rangeBounds) {
    const MATH::Vec3 center = (cluster.localBounds.min + cluster.localBounds.max) * 0.5f;
    const MATH::Vec3 extent = rangeBounds.max - rangeBounds.min;
    return EncodeMorton3D(QuantizeMortonAxis(center.x, rangeBounds.min.x, extent.x),
                          QuantizeMortonAxis(center.y, rangeBounds.min.y, extent.y),
                          QuantizeMortonAxis(center.z, rangeBounds.min.z, extent.z));
}

void SpatialSortClusterRangeForPages(uint32_t firstCluster, uint32_t clusterCount, ClusteredGeometryAsset& asset) {
    if (clusterCount <= 1u || firstCluster >= asset.clusters.size() ||
        firstCluster + clusterCount > asset.clusters.size()) {
        return;
    }

    Bounds rangeBounds{};
    bool hasBounds = false;
    for (uint32_t i = 0; i < clusterCount; ++i) {
        const MeshCluster& cluster = asset.clusters[firstCluster + i];
        if (!BOUNDS::IsUsable(cluster.localBounds)) {
            continue;
        }
        if (!hasBounds) {
            rangeBounds = cluster.localBounds;
        } else {
            rangeBounds.min.x = (std::min)(rangeBounds.min.x, cluster.localBounds.min.x);
            rangeBounds.min.y = (std::min)(rangeBounds.min.y, cluster.localBounds.min.y);
            rangeBounds.min.z = (std::min)(rangeBounds.min.z, cluster.localBounds.min.z);
            rangeBounds.max.x = (std::max)(rangeBounds.max.x, cluster.localBounds.max.x);
            rangeBounds.max.y = (std::max)(rangeBounds.max.y, cluster.localBounds.max.y);
            rangeBounds.max.z = (std::max)(rangeBounds.max.z, cluster.localBounds.max.z);
        }
        hasBounds = true;
    }
    if (!hasBounds || !BOUNDS::IsUsable(rangeBounds)) {
        return;
    }

    auto begin = asset.clusters.begin() + firstCluster;
    auto end = begin + clusterCount;
    std::stable_sort(begin, end, [&](const MeshCluster& a, const MeshCluster& b) {
        const uint32_t mortonA = ClusterMortonCode(a, rangeBounds);
        const uint32_t mortonB = ClusterMortonCode(b, rangeBounds);
        if (mortonA != mortonB) {
            return mortonA < mortonB;
        }
        if (a.firstIndex != b.firstIndex) {
            return a.firstIndex < b.firstIndex;
        }
        return a.firstPrimitive < b.firstPrimitive;
    });

    // metadata の Morton 順と実際の読み出し順を一致させる。range は
    // 直前に append された一つの section/LOD で、他の range と共有しない。
    uint32_t firstVertex = UINT32_MAX, firstIndex = UINT32_MAX, firstPrimitive = UINT32_MAX;
    size_t vertexCount = 0, indexCount = 0, primitiveCount = 0;
    for (auto cluster = begin; cluster != end; ++cluster) {
        firstVertex = (std::min)(firstVertex, cluster->firstVertex);
        firstIndex = (std::min)(firstIndex, cluster->firstIndex);
        firstPrimitive = (std::min)(firstPrimitive, cluster->firstPrimitive);
        vertexCount += cluster->vertexCount;
        indexCount += cluster->indexCount;
        primitiveCount += cluster->primitiveCount;
    }
    std::vector<ClusterVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<MeshletPrimitive> primitives;
    vertices.reserve(vertexCount);
    indices.reserve(indexCount);
    primitives.reserve(primitiveCount);
    for (auto cluster = begin; cluster != end; ++cluster) {
        const uint32_t nextVertex = firstVertex + static_cast<uint32_t>(vertices.size());
        const uint32_t nextIndex = firstIndex + static_cast<uint32_t>(indices.size());
        const uint32_t nextPrimitive = firstPrimitive + static_cast<uint32_t>(primitives.size());
        vertices.insert(vertices.end(), asset.packedVertices.begin() + cluster->firstVertex,
                        asset.packedVertices.begin() + cluster->firstVertex + cluster->vertexCount);
        indices.insert(indices.end(), asset.packedIndices.begin() + cluster->firstIndex,
                       asset.packedIndices.begin() + cluster->firstIndex + cluster->indexCount);
        primitives.insert(primitives.end(), asset.meshletPrimitives.begin() + cluster->firstPrimitive,
                          asset.meshletPrimitives.begin() + cluster->firstPrimitive + cluster->primitiveCount);
        cluster->firstVertex = nextVertex;
        cluster->firstIndex = nextIndex;
        cluster->firstPrimitive = nextPrimitive;
    }
    std::copy(vertices.begin(), vertices.end(), asset.packedVertices.begin() + firstVertex);
    std::copy(indices.begin(), indices.end(), asset.packedIndices.begin() + firstIndex);
    std::copy(primitives.begin(), primitives.end(), asset.meshletPrimitives.begin() + firstPrimitive);
}

uint32_t AppendClusterGeometry(const SurfaceCookInput& work, const std::vector<SourceTriangle>& triangles,
                               const std::vector<uint32_t>& group, uint32_t surfaceIndex,
                               const ClusteredGeometryCookSettings& settings, ClusteredGeometryAsset& asset) {
    std::unordered_map<uint32_t, uint32_t> vertexRemap{};
    vertexRemap.reserve(group.size() * 3u);

    MeshCluster cluster{};
    cluster.surfaceIndex = surfaceIndex;
    cluster.firstIndex = static_cast<uint32_t>(asset.packedIndices.size());
    cluster.firstVertex = static_cast<uint32_t>(asset.packedVertices.size());
    cluster.triangleCount = static_cast<uint32_t>(group.size());
    cluster.firstPrimitive = static_cast<uint32_t>(asset.meshletPrimitives.size());
    cluster.primitiveCount = cluster.triangleCount;

    Bounds bounds = BOUNDS::EmptyBounds();
    bool hasBounds = false;
    for (uint32_t triangleIndex : group) {
        const SourceTriangle& tri = triangles[triangleIndex];
        const uint32_t sourceIndices[3] = {tri.i0, tri.i1, tri.i2};
        uint32_t localIndices[3]{};
        for (uint32_t corner = 0; corner < 3u; ++corner) {
            const uint32_t sourceIndex = sourceIndices[corner];
            auto it = vertexRemap.find(sourceIndex);
            if (it == vertexRemap.end()) {
                const uint32_t localIndex = static_cast<uint32_t>(asset.packedVertices.size()) - cluster.firstVertex;
                vertexRemap[sourceIndex] = localIndex;
                asset.packedVertices.push_back(work.vertices[sourceIndex]);
                if (MATH::IsFinite(work.vertices[sourceIndex].position)) {
                    BOUNDS::Encapsulate(bounds, work.vertices[sourceIndex].position);
                    hasBounds = true;
                }
                // cluster 内 index は meshlet と同じく局所 index として保存する。
                localIndices[corner] = localIndex;
            } else {
                localIndices[corner] = it->second;
            }
        }
        asset.packedIndices.push_back(localIndices[0]);
        asset.packedIndices.push_back(localIndices[1]);
        asset.packedIndices.push_back(localIndices[2]);

        MeshletPrimitive primitive{};
        primitive.i0 = localIndices[0];
        primitive.i1 = localIndices[1];
        primitive.i2 = localIndices[2];
        asset.meshletPrimitives.push_back(primitive);
    }

    cluster.indexCount = static_cast<uint32_t>(asset.packedIndices.size()) - cluster.firstIndex;
    cluster.vertexCount = static_cast<uint32_t>(asset.packedVertices.size()) - cluster.firstVertex;
    // 最終 group 確定後に一度だけ最適化する。頂点属性/skin/UV と両 index
    // stream を同じ remap で更新し、後段 compaction に順序を壊させない。
    if (cluster.vertexCount <= RENDER3D::CLUSTER::kHcmeshMaxVerticesPerCluster &&
        cluster.primitiveCount <= RENDER3D::CLUSTER::kHcmeshMaxTrianglesPerCluster) {
        std::array<unsigned int, RENDER3D::CLUSTER::kHcmeshMaxVerticesPerCluster> remap{};
        std::array<ClusterVertex, RENDER3D::CLUSTER::kHcmeshMaxVerticesPerCluster> vertices{};
        std::array<unsigned char, RENDER3D::CLUSTER::kHcmeshMaxTrianglesPerCluster * 3u> indices{};
        for (uint32_t i = 0; i < cluster.vertexCount; ++i) {
            remap[i] = i;
            vertices[i] = asset.packedVertices[cluster.firstVertex + i];
        }
        for (uint32_t i = 0; i < cluster.indexCount; ++i)
            indices[i] = static_cast<unsigned char>(asset.packedIndices[cluster.firstIndex + i]);
        meshopt_optimizeMeshlet(remap.data(), indices.data(), cluster.primitiveCount, cluster.vertexCount);
        for (uint32_t i = 0; i < cluster.vertexCount; ++i)
            asset.packedVertices[cluster.firstVertex + i] = vertices[remap[i]];
        for (uint32_t i = 0; i < cluster.indexCount; ++i)
            asset.packedIndices[cluster.firstIndex + i] = indices[i];
        for (uint32_t i = 0; i < cluster.primitiveCount; ++i) {
            auto& primitive = asset.meshletPrimitives[cluster.firstPrimitive + i];
            primitive.i0 = indices[i * 3u];
            primitive.i1 = indices[i * 3u + 1u];
            primitive.i2 = indices[i * 3u + 2u];
        }
    }
    cluster.localBounds = hasBounds ? bounds : Bounds{};
    cluster.sphereCenter = (cluster.localBounds.min + cluster.localBounds.max) * 0.5f;
    cluster.sphereRadius = 0.0f;
    for (uint32_t i = 0; i < cluster.vertexCount; ++i) {
        const ClusterVertex& vertex = asset.packedVertices[cluster.firstVertex + i];
        cluster.sphereRadius = (std::max)(cluster.sphereRadius, MATH::Length(vertex.position - cluster.sphereCenter));
    }

    cluster.coneApex = cluster.sphereCenter;
    cluster.coneAxis = {0.0f, 1.0f, 0.0f};
    cluster.coneCutoff = 1.0f;
    if (settings.buildNormalCone) {
        MeshCluster meshoptCluster = cluster;
        if (ApplyMeshoptMeshletBounds(asset, cluster, meshoptCluster)) {
            cluster = meshoptCluster;
        }
    }

    cluster.flags = work.flags;
    asset.clusters.push_back(cluster);
    return static_cast<uint32_t>(asset.clusters.size() - 1u);
}

void AppendPagesForClusterRange(uint32_t firstCluster, uint32_t clusterCount,
                                const ClusteredGeometryCookSettings& settings, ClusteredGeometryAsset& asset,
                                uint32_t& outFirstPage, uint32_t& outPageCount) {
    outFirstPage = static_cast<uint32_t>(asset.pages.size());
    outPageCount = 0u;
    if (!settings.buildClusterPages || clusterCount == 0u) {
        return;
    }

    SpatialSortClusterRangeForPages(firstCluster, clusterCount, asset);

    const uint32_t maxPerPage = (std::max)(1u, settings.maxClustersPerPage);
    uint32_t remaining = clusterCount;
    uint32_t clusterCursor = firstCluster;
    while (remaining > 0u) {
        const uint32_t pageClusterCount = (std::min)(remaining, maxPerPage);
        ClusterPage page{};
        page.firstCluster = clusterCursor;
        page.clusterCount = pageClusterCount;

        Bounds bounds{};
        bool hasBounds = false;
        const uint32_t invalidOffset = (std::numeric_limits<uint32_t>::max)();
        uint32_t minIndex = invalidOffset;
        uint32_t minVertex = invalidOffset;
        uint32_t minPrimitive = invalidOffset;
        uint32_t endIndex = 0u;
        uint32_t endVertex = 0u;
        uint32_t endPrimitive = 0u;
        for (uint32_t i = 0; i < pageClusterCount; ++i) {
            const MeshCluster& cluster = asset.clusters[clusterCursor + i];
            bounds = hasBounds ? MergeBounds(bounds, cluster.localBounds) : cluster.localBounds;
            hasBounds = hasBounds || BOUNDS::IsUsable(cluster.localBounds);
            minIndex = (std::min)(minIndex, cluster.firstIndex);
            minVertex = (std::min)(minVertex, cluster.firstVertex);
            minPrimitive = (std::min)(minPrimitive, cluster.firstPrimitive);
            endIndex = (std::max)(endIndex, cluster.firstIndex + cluster.indexCount);
            endVertex = (std::max)(endVertex, cluster.firstVertex + cluster.vertexCount);
            endPrimitive = (std::max)(endPrimitive, cluster.firstPrimitive + cluster.primitiveCount);
        }
        page.firstIndex = minIndex != invalidOffset ? minIndex : 0u;
        page.firstVertex = minVertex != invalidOffset ? minVertex : 0u;
        page.firstPrimitive = minPrimitive != invalidOffset ? minPrimitive : 0u;
        page.indexCount = endIndex >= page.firstIndex ? endIndex - page.firstIndex : 0u;
        page.vertexCount = endVertex >= page.firstVertex ? endVertex - page.firstVertex : 0u;
        page.primitiveCount = endPrimitive >= page.firstPrimitive ? endPrimitive - page.firstPrimitive : 0u;
        page.localBounds = hasBounds ? bounds : Bounds{};
        asset.pages.push_back(page);

        clusterCursor += pageClusterCount;
        remaining -= pageClusterCount;
    }
    outPageCount = static_cast<uint32_t>(asset.pages.size()) - outFirstPage;
}

void AppendLod0RangeForSection(ClusterSurfaceSection& section, uint32_t surfaceIndex,
                               const ClusteredGeometryCookSettings& settings, ClusteredGeometryAsset& asset) {
    ClusterSurfaceLodRange lodRange{};
    lodRange.surfaceIndex = surfaceIndex;
    lodRange.lodIndex = 0u;
    lodRange.firstCluster = section.firstCluster;
    lodRange.clusterCount = section.clusterCount;
    lodRange.firstIndex = section.firstIndex;
    lodRange.indexCount = section.indexCount;
    lodRange.firstVertex = section.firstVertex;
    lodRange.vertexCount = section.vertexCount;
    lodRange.firstPage = section.firstPage;
    lodRange.pageCount = section.pageCount;
    lodRange.firstPrimitive = section.firstPrimitive;
    lodRange.primitiveCount = section.primitiveCount;
    lodRange.minScreenRadius = ResolveLodMinScreenRadius(0u, settings);
    lodRange.flags = section.flags;
    lodRange.sectionIndex = section.sectionIndex;

    section.firstLodRange = static_cast<uint32_t>(asset.surfaceLodRanges.size());
    section.lodRangeCount = 1u;
    asset.surfaceLodRanges.push_back(lodRange);
}

bool AppendReducedLodRangeForSection(ClusterSurfaceSection& section, uint32_t surfaceIndex, uint32_t lodIndex,
                                     const SurfaceCookInput& lodWork, float geometricError,
                                     const ClusteredGeometryCookSettings& settings, ClusteredGeometryAsset& asset) {
    const std::vector<SourceTriangle> lodTriangles = BuildTriangles(lodWork.vertices, lodWork.indices);
    if (lodTriangles.empty()) {
        return false;
    }

    ClusterSurfaceLodRange lodRange{};
    lodRange.surfaceIndex = surfaceIndex;
    lodRange.lodIndex = lodIndex;
    lodRange.firstCluster = static_cast<uint32_t>(asset.clusters.size());
    lodRange.firstIndex = static_cast<uint32_t>(asset.packedIndices.size());
    lodRange.firstVertex = static_cast<uint32_t>(asset.packedVertices.size());
    lodRange.firstPrimitive = static_cast<uint32_t>(asset.meshletPrimitives.size());
    lodRange.geometricError = geometricError;
    lodRange.minScreenRadius = ResolveLodMinScreenRadius(lodIndex, settings);
    lodRange.flags = section.flags;
    lodRange.sectionIndex = section.sectionIndex;

    const std::vector<std::vector<uint32_t>> lodGroups =
        BuildClusterTriangleGroups(lodWork.vertices, lodTriangles, settings);
    for (const std::vector<uint32_t>& group : lodGroups) {
        if (group.empty()) {
            continue;
        }
        AppendClusterGeometry(lodWork, lodTriangles, group, surfaceIndex, settings, asset);
    }

    lodRange.clusterCount = static_cast<uint32_t>(asset.clusters.size()) - lodRange.firstCluster;
    lodRange.indexCount = static_cast<uint32_t>(asset.packedIndices.size()) - lodRange.firstIndex;
    lodRange.vertexCount = static_cast<uint32_t>(asset.packedVertices.size()) - lodRange.firstVertex;
    lodRange.primitiveCount = static_cast<uint32_t>(asset.meshletPrimitives.size()) - lodRange.firstPrimitive;
    if (lodRange.clusterCount == 0u || lodRange.indexCount == 0u || lodRange.vertexCount == 0u ||
        lodRange.primitiveCount == 0u) {
        return false;
    }

    AppendPagesForClusterRange(lodRange.firstCluster, lodRange.clusterCount, settings, asset, lodRange.firstPage,
                               lodRange.pageCount);
    if (lodRange.pageCount == 0u && settings.buildClusterPages) {
        return false;
    }

    asset.surfaceLodRanges.push_back(lodRange);
    ++section.lodRangeCount;
    return true;
}

void AppendReducedLodRangesForSection(ClusterSurfaceSection& section, uint32_t surfaceIndex,
                                      const SurfaceCookInput& sourceWork, const ClusteredGeometryCookSettings& settings,
                                      bool lockSectionBorders, ClusteredGeometryAsset& asset) {
    SurfaceCookInput currentSource = sourceWork;
    uint32_t previousTriangleCount = section.primitiveCount;
    float accumulatedError = 0.0f;
    const uint32_t maxLodCount = (std::min)(settings.maxSurfaceLodCount, 5u);
    for (uint32_t lodIndex = 1u; lodIndex < maxLodCount; ++lodIndex) {
        SurfaceLodResult lodResult{};
        if (!BuildReducedSurfaceLod(currentSource, lodIndex, previousTriangleCount, settings, lockSectionBorders,
                                    lodResult)) {
            // L1 の厳しい予算で落とせなくても、遠景の L2-L4 は試す。
            continue;
        }

        const uint32_t lodTriangleCount = CountSurfaceTriangles(lodResult.work);
        if (lodTriangleCount == 0u || lodTriangleCount >= previousTriangleCount) {
            break;
        }

        if (AppendReducedLodRangeForSection(section, surfaceIndex, lodIndex, lodResult.work,
                                            accumulatedError + lodResult.geometricError, settings, asset)) {
            previousTriangleCount = lodTriangleCount;
            accumulatedError += lodResult.geometricError;
            currentSource = std::move(lodResult.work);
        } else {
            break;
        }
    }
}

bool AssembleClusteredSurface(const SurfaceCookInput& work, const ClusteredGeometryCookSettings& settings,
                              ClusteredGeometryAsset& asset, ClusteredGeometryBuildReport& report) {
    if (!settings.buildPackedGeometry || work.vertices.empty() || work.indices.size() < 3u) {
        ++report.skippedInvalidPrimitiveCount;
        return false;
    }

    const SurfaceCookInput buildWork = SubdivideLargeStaticTriangles(work, settings, report);
    const std::vector<SourceTriangle> triangles = BuildTriangles(buildWork.vertices, buildWork.indices);
    if (triangles.empty()) {
        ++report.skippedInvalidPrimitiveCount;
        return false;
    }

    ClusterSurface surface{};
    surface.nodeIndex = buildWork.nodeIndex;
    surface.meshIndex = buildWork.meshIndex;
    surface.primitiveIndex = buildWork.primitiveIndex;
    surface.materialIndex = buildWork.materialIndex;
    const uint32_t surfaceIndex = static_cast<uint32_t>(asset.surfaces.size());
    surface.firstCluster = static_cast<uint32_t>(asset.clusters.size());
    surface.firstIndex = static_cast<uint32_t>(asset.packedIndices.size());
    surface.firstVertex = static_cast<uint32_t>(asset.packedVertices.size());
    surface.firstPrimitive = static_cast<uint32_t>(asset.meshletPrimitives.size());
    surface.firstSection = static_cast<uint32_t>(asset.surfaceSections.size());
    surface.flags = buildWork.flags;

    std::vector<SurfaceSectionPlan> sectionSources{};
    const bool partitioned =
        BuildLargeStaticSurfaceSectionPlans(buildWork, triangles, settings, report, sectionSources);
    if (!partitioned) {
        SurfaceSectionPlan wholeSurface{};
        wholeSurface.triangleIndices.resize(triangles.size());
        std::iota(wholeSurface.triangleIndices.begin(), wholeSurface.triangleIndices.end(), 0u);
        wholeSurface.groups = BuildClusterTriangleGroups(buildWork.vertices, triangles, settings, &report);
        sectionSources.push_back(std::move(wholeSurface));
    }

    std::vector<ClusterSurfaceSection> sections{};
    std::vector<SurfaceCookInput> sectionWorks{};
    sections.reserve(sectionSources.size());
    sectionWorks.reserve(sectionSources.size());

    for (const SurfaceSectionPlan& sectionSource : sectionSources) {
        ClusterSurfaceSection section{};
        section.surfaceIndex = surfaceIndex;
        section.sectionIndex = static_cast<uint32_t>(sections.size());
        section.firstCluster = static_cast<uint32_t>(asset.clusters.size());
        section.firstIndex = static_cast<uint32_t>(asset.packedIndices.size());
        section.firstVertex = static_cast<uint32_t>(asset.packedVertices.size());
        section.firstPrimitive = static_cast<uint32_t>(asset.meshletPrimitives.size());
        section.flags = buildWork.flags;

        SurfaceCookInput sectionWork =
            partitioned ? BuildSectionCookInput(buildWork, triangles, sectionSource.triangleIndices) : buildWork;
        if (sectionWork.vertices.empty() || sectionWork.indices.size() < 3u) {
            continue;
        }

        for (const std::vector<uint32_t>& group : sectionSource.groups) {
            if (group.empty()) {
                continue;
            }
            AppendClusterGeometry(buildWork, triangles, group, surfaceIndex, settings, asset);
        }

        section.clusterCount = static_cast<uint32_t>(asset.clusters.size()) - section.firstCluster;
        section.indexCount = static_cast<uint32_t>(asset.packedIndices.size()) - section.firstIndex;
        section.vertexCount = static_cast<uint32_t>(asset.packedVertices.size()) - section.firstVertex;
        section.primitiveCount = static_cast<uint32_t>(asset.meshletPrimitives.size()) - section.firstPrimitive;
        section.localBounds = ComputeTriangleSubsetBounds(triangles, sectionSource.triangleIndices);
        if (!BOUNDS::IsUsable(section.localBounds)) {
            section.localBounds = ComputeVertexBounds(buildWork.vertices);
        }
        ConfigureSectionLodMetric(section, settings, section.localBounds);
        if (section.clusterCount == 0u) {
            continue;
        }

        sections.push_back(section);
        sectionWorks.push_back(std::move(sectionWork));
    }

    surface.clusterCount = static_cast<uint32_t>(asset.clusters.size()) - surface.firstCluster;
    surface.indexCount = static_cast<uint32_t>(asset.packedIndices.size()) - surface.firstIndex;
    surface.vertexCount = static_cast<uint32_t>(asset.packedVertices.size()) - surface.firstVertex;
    surface.primitiveCount = static_cast<uint32_t>(asset.meshletPrimitives.size()) - surface.firstPrimitive;
    surface.localBounds = ComputeVertexBounds(buildWork.vertices);
    surface.firstLodRange = static_cast<uint32_t>(asset.surfaceLodRanges.size());
    if (surface.clusterCount == 0u || sections.empty()) {
        ++report.skippedInvalidPrimitiveCount;
        return false;
    }

    for (size_t sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex) {
        ClusterSurfaceSection& section = sections[sectionIndex];
        AppendPagesForClusterRange(section.firstCluster, section.clusterCount, settings, asset, section.firstPage,
                                   section.pageCount);
        if (section.pageCount == 0u && settings.buildClusterPages) {
            continue;
        }

        AppendLod0RangeForSection(section, surfaceIndex, settings, asset);
        AppendReducedLodRangesForSection(section, surfaceIndex, sectionWorks[sectionIndex], settings,
                                         partitioned && settings.lockPartitionBorders, asset);
        asset.surfaceSections.push_back(section);
    }

    surface.sectionCount = static_cast<uint32_t>(asset.surfaceSections.size()) - surface.firstSection;
    surface.lodRangeCount = static_cast<uint32_t>(asset.surfaceLodRanges.size()) - surface.firstLodRange;
    if (surface.sectionCount == 0u || surface.lodRangeCount == 0u) {
        ++report.skippedInvalidPrimitiveCount;
        return false;
    }

    asset.surfaces.push_back(surface);
    return true;
}

} // namespace HIKARI::ASSETS::GEOMETRY::COOKING
