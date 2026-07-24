#include "Assets/Geometry/HIKARI_ClusteredGeometryValidator.h"

#include <cmath>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::GEOMETRY {

    namespace {
        bool RangeValid(uint32_t first, uint32_t count, size_t size) {
            const uint64_t begin = first;
            const uint64_t end = begin + count;
            return end <= static_cast<uint64_t>(size);
        }

        bool RangeContains(uint32_t outerFirst, uint32_t outerCount, uint32_t innerFirst, uint32_t innerCount) {
            const uint64_t outerBegin = outerFirst;
            const uint64_t outerEnd = outerBegin + outerCount;
            const uint64_t innerBegin = innerFirst;
            const uint64_t innerEnd = innerBegin + innerCount;
            return innerBegin >= outerBegin && innerEnd <= outerEnd;
        }

        bool IsFiniteBounds(const Bounds& bounds) {
            return MATH::IsFinite(bounds.min) && MATH::IsFinite(bounds.max);
        }

        uint32_t CountAlphaFlags(uint32_t flags) {
            uint32_t count = 0;
            count += RENDER3D::CLUSTER::HasFlag(flags, RENDER3D::CLUSTER::ClusterSurfaceFlags::Opaque) ? 1u : 0u;
            count += RENDER3D::CLUSTER::HasFlag(flags, RENDER3D::CLUSTER::ClusterSurfaceFlags::AlphaMask) ? 1u : 0u;
            count += RENDER3D::CLUSTER::HasFlag(flags, RENDER3D::CLUSTER::ClusterSurfaceFlags::Transparent) ? 1u : 0u;
            return count;
        }

        bool VertexFinite(const RENDER3D::CLUSTER::ClusterVertex& vertex) {
            return MATH::IsFinite(vertex.position) &&
                MATH::IsFinite(vertex.normal) &&
                MATH::IsFinite(vertex.tangent) &&
                MATH::IsFinite(vertex.uv0) &&
                MATH::IsFinite(vertex.uv1) &&
                MATH::IsFinite(vertex.color);
        }

        bool ClusterMicroIndicesValid(
            const RENDER3D::CLUSTER::ClusteredGeometryAsset& asset,
            const RENDER3D::CLUSTER::MeshCluster& cluster) {

            if (!RangeValid(cluster.firstIndex, cluster.indexCount, asset.packedIndices.size())) {
                return false;
            }

            for (uint32_t i = 0; i < cluster.indexCount; ++i) {
                const uint32_t localIndex = asset.packedIndices[cluster.firstIndex + i];
                if (localIndex >= cluster.vertexCount) {
                    return false;
                }
            }
            return true;
        }

        bool MeshletPrimitiveTableValid(
            const RENDER3D::CLUSTER::ClusteredGeometryAsset& asset,
            const RENDER3D::CLUSTER::MeshCluster& cluster) {

            if (cluster.primitiveCount != cluster.triangleCount ||
                !RangeValid(cluster.firstPrimitive, cluster.primitiveCount, asset.meshletPrimitives.size())) {
                return false;
            }

            for (uint32_t i = 0; i < cluster.primitiveCount; ++i) {
                const RENDER3D::CLUSTER::MeshletPrimitive& primitive =
                    asset.meshletPrimitives[cluster.firstPrimitive + i];
                if (primitive.i0 >= cluster.vertexCount ||
                    primitive.i1 >= cluster.vertexCount ||
                    primitive.i2 >= cluster.vertexCount) {
                    return false;
                }

                const uint32_t packedIndex = cluster.firstIndex + i * 3u;
                if (packedIndex + 2u >= asset.packedIndices.size() ||
                    asset.packedIndices[packedIndex + 0u] != primitive.i0 ||
                    asset.packedIndices[packedIndex + 1u] != primitive.i1 ||
                    asset.packedIndices[packedIndex + 2u] != primitive.i2) {
                    return false;
                }
            }
            return true;
        }

        bool RequiredAssetFlagsValid(const RENDER3D::CLUSTER::ClusteredGeometryAsset& asset) {
            using RENDER3D::CLUSTER::ClusteredGeometryFlags;
            return RENDER3D::CLUSTER::HasFlag(asset.flags, ClusteredGeometryFlags::NodeTransformBaked) &&
                RENDER3D::CLUSTER::HasFlag(asset.flags, ClusteredGeometryFlags::ClusterLocalIndices) &&
                RENDER3D::CLUSTER::HasFlag(asset.flags, ClusteredGeometryFlags::SourceMapping) &&
                RENDER3D::CLUSTER::HasFlag(asset.flags, ClusteredGeometryFlags::MeshletPrimitiveTable) &&
                RENDER3D::CLUSTER::HasFlag(asset.flags, ClusteredGeometryFlags::MeshletReady) &&
                RENDER3D::CLUSTER::HasFlag(asset.flags, ClusteredGeometryFlags::LodRanges);
        }

        bool SurfaceSourceMappingValid(const RENDER3D::CLUSTER::ClusterSurface& surface) {
            return surface.meshIndex != RENDER3D::CLUSTER::kInvalidClusterIndex &&
                surface.primitiveIndex != RENDER3D::CLUSTER::kInvalidClusterIndex;
        }

        void AddMessage(
            ClusteredGeometryValidationResult& result,
            const std::string& message) {
            if (result.messages.size() < 32u) {
                result.messages.push_back(message);
            }
        }
    }

    ClusteredGeometryValidationResult ValidateClusteredGeometryAsset(
        const RENDER3D::CLUSTER::ClusteredGeometryAsset& asset) {

        ClusteredGeometryValidationResult result{};
        bool assetMetadataValid = true;

        if (asset.surfaces.empty()) {
            AddMessage(result, "no surfaces");
        }
        if (asset.clusters.empty()) {
            AddMessage(result, "no clusters");
        }
        if (asset.packedVertices.empty()) {
            AddMessage(result, "no packed vertices");
        }
        if (asset.packedIndices.empty()) {
            AddMessage(result, "no packed indices");
        }
        if (asset.meshletPrimitives.empty()) {
            AddMessage(result, "no meshlet primitives");
        }
        if (asset.surfaceLodRanges.empty()) {
            AddMessage(result, "no surface lod ranges");
        }
        if (asset.surfaceSections.empty()) {
            AddMessage(result, "no surface sections");
        }
        if (!asset.sourceModelGuid.IsValid()) {
            assetMetadataValid = false;
            AddMessage(result, "missing source model guid");
        }
        if (asset.sourceModelPath.empty()) {
            assetMetadataValid = false;
            AddMessage(result, "missing source model path");
        }
        if (!RequiredAssetFlagsValid(asset)) {
            assetMetadataValid = false;
            AddMessage(result, "missing clustered geometry asset flags");
        }
        if (!IsFiniteBounds(asset.localBounds) || !BOUNDS::IsUsable(asset.localBounds)) {
            ++result.invalidBoundsCount;
            AddMessage(result, "invalid asset bounds");
        }

        for (size_t i = 0; i < asset.packedVertices.size(); ++i) {
            if (!VertexFinite(asset.packedVertices[i])) {
                ++result.invalidBoundsCount;
                AddMessage(result, "invalid packed vertex " + std::to_string(i));
                break;
            }
        }

        for (size_t i = 0; i < asset.surfaces.size(); ++i) {
            const RENDER3D::CLUSTER::ClusterSurface& surface = asset.surfaces[i];
            bool surfaceValid = true;
            if (surface.clusterCount == 0u ||
                surface.indexCount == 0u ||
                surface.vertexCount == 0u ||
                surface.primitiveCount == 0u ||
                (surface.indexCount % 3u) != 0u ||
                surface.primitiveCount * 3u != surface.indexCount ||
                !RangeValid(surface.firstCluster, surface.clusterCount, asset.clusters.size()) ||
                !RangeValid(surface.firstIndex, surface.indexCount, asset.packedIndices.size()) ||
                !RangeValid(surface.firstVertex, surface.vertexCount, asset.packedVertices.size()) ||
                !RangeValid(surface.firstPrimitive, surface.primitiveCount, asset.meshletPrimitives.size()) ||
                !RangeValid(surface.firstPage, surface.pageCount, asset.pages.size()) ||
                !RangeValid(surface.firstLodRange, surface.lodRangeCount, asset.surfaceLodRanges.size()) ||
                surface.sectionCount == 0u ||
                !RangeValid(surface.firstSection, surface.sectionCount, asset.surfaceSections.size())) {
                surfaceValid = false;
            }
            if (surface.materialIndex >= asset.materialSlotMapping.size()) {
                ++result.invalidMaterialCount;
                surfaceValid = false;
            }
            if (!SurfaceSourceMappingValid(surface)) {
                surfaceValid = false;
                AddMessage(result, "missing source mapping surface " + std::to_string(i));
            }
            if (CountAlphaFlags(surface.flags) != 1u) {
                surfaceValid = false;
            }
            if (!IsFiniteBounds(surface.localBounds) || !BOUNDS::IsUsable(surface.localBounds)) {
                ++result.invalidBoundsCount;
                surfaceValid = false;
            }
            if (surfaceValid) {
                bool hasLod0 = false;
                for (uint32_t lodOffset = 0; lodOffset < surface.lodRangeCount; ++lodOffset) {
                    const RENDER3D::CLUSTER::ClusterSurfaceLodRange& lodRange =
                        asset.surfaceLodRanges[surface.firstLodRange + lodOffset];
                    const bool rangeValid =
                        lodRange.surfaceIndex == i &&
                        lodRange.sectionIndex < surface.sectionCount &&
                        lodRange.clusterCount != 0u &&
                        lodRange.indexCount != 0u &&
                        lodRange.vertexCount != 0u &&
                        lodRange.primitiveCount != 0u &&
                        lodRange.primitiveCount * 3u == lodRange.indexCount &&
                        MATH::IsFinite(lodRange.geometricError) &&
                        MATH::IsFinite(lodRange.minScreenRadius) &&
                        RangeValid(lodRange.firstCluster, lodRange.clusterCount, asset.clusters.size()) &&
                        RangeValid(lodRange.firstIndex, lodRange.indexCount, asset.packedIndices.size()) &&
                        RangeValid(lodRange.firstVertex, lodRange.vertexCount, asset.packedVertices.size()) &&
                        RangeValid(lodRange.firstPage, lodRange.pageCount, asset.pages.size()) &&
                        RangeValid(lodRange.firstPrimitive, lodRange.primitiveCount, asset.meshletPrimitives.size());
                    if (!rangeValid) {
                        surfaceValid = false;
                        break;
                    }
                    if (lodRange.lodIndex == 0u) {
                        hasLod0 = true;
                    }
                    if (rangeValid) {
                        for (uint32_t clusterOffset = 0; clusterOffset < lodRange.clusterCount; ++clusterOffset) {
                            const RENDER3D::CLUSTER::MeshCluster& cluster =
                                asset.clusters[lodRange.firstCluster + clusterOffset];
                            if (cluster.surfaceIndex != i ||
                                !RangeContains(lodRange.firstIndex, lodRange.indexCount, cluster.firstIndex, cluster.indexCount) ||
                                !RangeContains(lodRange.firstVertex, lodRange.vertexCount, cluster.firstVertex, cluster.vertexCount) ||
                                !RangeContains(lodRange.firstPrimitive, lodRange.primitiveCount, cluster.firstPrimitive, cluster.primitiveCount)) {
                                surfaceValid = false;
                                break;
                            }
                        }
                    }
                    if (!surfaceValid) {
                        break;
                    }
                    if (rangeValid) {
                        for (uint32_t pageOffset = 0; pageOffset < lodRange.pageCount; ++pageOffset) {
                            const RENDER3D::CLUSTER::ClusterPage& page =
                                asset.pages[lodRange.firstPage + pageOffset];
                            if (!RangeContains(lodRange.firstCluster, lodRange.clusterCount, page.firstCluster, page.clusterCount) ||
                                !RangeContains(lodRange.firstIndex, lodRange.indexCount, page.firstIndex, page.indexCount) ||
                                !RangeContains(lodRange.firstVertex, lodRange.vertexCount, page.firstVertex, page.vertexCount) ||
                                !RangeContains(lodRange.firstPrimitive, lodRange.primitiveCount, page.firstPrimitive, page.primitiveCount)) {
                                surfaceValid = false;
                                break;
                            }
                        }
                    }
                    if (!surfaceValid) {
                        break;
                    }
                }
                if (!hasLod0) {
                    surfaceValid = false;
                }
            }
            if (surfaceValid) {
                for (uint32_t clusterOffset = 0; clusterOffset < surface.clusterCount; ++clusterOffset) {
                    const RENDER3D::CLUSTER::MeshCluster& cluster =
                        asset.clusters[surface.firstCluster + clusterOffset];
                    if (cluster.surfaceIndex != i ||
                        !RangeContains(surface.firstIndex, surface.indexCount, cluster.firstIndex, cluster.indexCount) ||
                        !RangeContains(surface.firstVertex, surface.vertexCount, cluster.firstVertex, cluster.vertexCount) ||
                        !RangeContains(surface.firstPrimitive, surface.primitiveCount, cluster.firstPrimitive, cluster.primitiveCount)) {
                        surfaceValid = false;
                        break;
                    }
                }
            }
            if (surfaceValid) {
                for (uint32_t pageOffset = 0; pageOffset < surface.pageCount; ++pageOffset) {
                    const RENDER3D::CLUSTER::ClusterPage& page =
                        asset.pages[surface.firstPage + pageOffset];
                    if (!RangeContains(surface.firstCluster, surface.clusterCount, page.firstCluster, page.clusterCount) ||
                        !RangeContains(surface.firstIndex, surface.indexCount, page.firstIndex, page.indexCount) ||
                        !RangeContains(surface.firstVertex, surface.vertexCount, page.firstVertex, page.vertexCount) ||
                        !RangeContains(surface.firstPrimitive, surface.primitiveCount, page.firstPrimitive, page.primitiveCount)) {
                        surfaceValid = false;
                        break;
                    }
                }
            }
            if (surfaceValid) {
                for (uint32_t sectionOffset = 0; sectionOffset < surface.sectionCount; ++sectionOffset) {
                    const RENDER3D::CLUSTER::ClusterSurfaceSection& section =
                        asset.surfaceSections[surface.firstSection + sectionOffset];
                    bool sectionValid =
                        section.surfaceIndex == i &&
                        section.sectionIndex == sectionOffset &&
                        section.clusterCount != 0u &&
                        section.indexCount != 0u &&
                        section.vertexCount != 0u &&
                        section.primitiveCount != 0u &&
                        section.primitiveCount * 3u == section.indexCount &&
                        RangeValid(section.firstCluster, section.clusterCount, asset.clusters.size()) &&
                        RangeValid(section.firstIndex, section.indexCount, asset.packedIndices.size()) &&
                        RangeValid(section.firstVertex, section.vertexCount, asset.packedVertices.size()) &&
                        RangeValid(section.firstPage, section.pageCount, asset.pages.size()) &&
                        RangeValid(section.firstPrimitive, section.primitiveCount, asset.meshletPrimitives.size()) &&
                        RangeValid(section.firstLodRange, section.lodRangeCount, asset.surfaceLodRanges.size()) &&
                        IsFiniteBounds(section.localBounds) &&
                        BOUNDS::IsUsable(section.localBounds);
                    bool hasSectionLod0 = false;
                    if (sectionValid) {
                        for (uint32_t lodOffset = 0; lodOffset < section.lodRangeCount; ++lodOffset) {
                            const RENDER3D::CLUSTER::ClusterSurfaceLodRange& lodRange =
                                asset.surfaceLodRanges[section.firstLodRange + lodOffset];
                            if (lodRange.surfaceIndex != i ||
                                lodRange.sectionIndex != section.sectionIndex) {
                                sectionValid = false;
                                break;
                            }
                            if (lodRange.lodIndex == 0u) {
                                hasSectionLod0 =
                                    lodRange.firstCluster == section.firstCluster &&
                                    lodRange.clusterCount == section.clusterCount &&
                                    lodRange.firstIndex == section.firstIndex &&
                                    lodRange.indexCount == section.indexCount &&
                                    lodRange.firstVertex == section.firstVertex &&
                                    lodRange.vertexCount == section.vertexCount &&
                                    lodRange.firstPage == section.firstPage &&
                                    lodRange.pageCount == section.pageCount &&
                                    lodRange.firstPrimitive == section.firstPrimitive &&
                                    lodRange.primitiveCount == section.primitiveCount;
                            }
                        }
                    }
                    if (!sectionValid || !hasSectionLod0) {
                        surfaceValid = false;
                        break;
                    }
                }
            }
            if (!surfaceValid) {
                ++result.invalidSurfaceCount;
                AddMessage(result, "invalid surface " + std::to_string(i));
            }
        }

        for (size_t i = 0; i < asset.clusters.size(); ++i) {
            const RENDER3D::CLUSTER::MeshCluster& cluster = asset.clusters[i];
            bool clusterValid = true;
            if (cluster.surfaceIndex >= asset.surfaces.size() ||
                cluster.triangleCount == 0u ||
                cluster.triangleCount > RENDER3D::CLUSTER::kHcmeshMaxTrianglesPerCluster ||
                cluster.vertexCount == 0u ||
                cluster.vertexCount > RENDER3D::CLUSTER::kHcmeshMaxVerticesPerCluster ||
                cluster.indexCount != cluster.triangleCount * 3u ||
                cluster.primitiveCount != cluster.triangleCount ||
                !RangeValid(cluster.firstIndex, cluster.indexCount, asset.packedIndices.size()) ||
                !RangeValid(cluster.firstVertex, cluster.vertexCount, asset.packedVertices.size()) ||
                !ClusterMicroIndicesValid(asset, cluster) ||
                !MeshletPrimitiveTableValid(asset, cluster)) {
                clusterValid = false;
            }
            if (!IsFiniteBounds(cluster.localBounds) ||
                !BOUNDS::IsUsable(cluster.localBounds) ||
                !MATH::IsFinite(cluster.sphereCenter) ||
                !MATH::IsFinite(cluster.coneApex) ||
                !MATH::IsFinite(cluster.coneAxis) ||
                !MATH::IsFinite(cluster.sphereRadius) ||
                !MATH::IsFinite(cluster.coneCutoff) ||
                cluster.sphereRadius <= 0.0f) {
                ++result.invalidBoundsCount;
                clusterValid = false;
            }
            if (!clusterValid) {
                ++result.invalidClusterCount;
                AddMessage(result, "invalid cluster " + std::to_string(i));
            }
        }

        for (size_t i = 0; i < asset.pages.size(); ++i) {
            const RENDER3D::CLUSTER::ClusterPage& page = asset.pages[i];
            bool pageValid = true;
            if (page.clusterCount == 0u ||
                page.clusterCount > RENDER3D::CLUSTER::kHcmeshMaxClustersPerPage ||
                page.primitiveCount * 3u != page.indexCount ||
                !RangeValid(page.firstCluster, page.clusterCount, asset.clusters.size()) ||
                !RangeValid(page.firstIndex, page.indexCount, asset.packedIndices.size()) ||
                !RangeValid(page.firstVertex, page.vertexCount, asset.packedVertices.size()) ||
                !RangeValid(page.firstPrimitive, page.primitiveCount, asset.meshletPrimitives.size())) {
                pageValid = false;
            }
            if (!IsFiniteBounds(page.localBounds) || !BOUNDS::IsUsable(page.localBounds)) {
                ++result.invalidBoundsCount;
                pageValid = false;
            }
            if (pageValid) {
                for (uint32_t clusterOffset = 0; clusterOffset < page.clusterCount; ++clusterOffset) {
                    const RENDER3D::CLUSTER::MeshCluster& cluster =
                        asset.clusters[page.firstCluster + clusterOffset];
                    if (!RangeContains(page.firstIndex, page.indexCount, cluster.firstIndex, cluster.indexCount) ||
                        !RangeContains(page.firstVertex, page.vertexCount, cluster.firstVertex, cluster.vertexCount) ||
                        !RangeContains(page.firstPrimitive, page.primitiveCount, cluster.firstPrimitive, cluster.primitiveCount)) {
                        pageValid = false;
                        break;
                    }
                }
            }
            if (!pageValid) {
                ++result.invalidPageCount;
                AddMessage(result, "invalid page " + std::to_string(i));
            }
        }

        result.valid =
            asset.valid &&
            assetMetadataValid &&
            !asset.surfaces.empty() &&
            !asset.surfaceLodRanges.empty() &&
            !asset.surfaceSections.empty() &&
            !asset.clusters.empty() &&
            !asset.packedVertices.empty() &&
            !asset.packedIndices.empty() &&
            !asset.meshletPrimitives.empty() &&
            result.invalidSurfaceCount == 0u &&
            result.invalidClusterCount == 0u &&
            result.invalidPageCount == 0u &&
            result.invalidMaterialCount == 0u;

        return result;
    }

} // namespace HIKARI::ASSETS::GEOMETRY
