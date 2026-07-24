#include "Assets/Geometry/Cooking/Internal/HIKARI_ClusteredGeometryCookInternal.h"

#include <algorithm>
#include <limits>
#include <string>

#include "Render3D/Cluster/HIKARI_ClusterGeometryPacked.h"

namespace HIKARI::ASSETS::GEOMETRY::COOKING {

    void FillCookReportFromAsset(const ClusteredGeometryAsset& asset, ClusteredGeometryBuildReport& report) {
        report.surfaceCount = static_cast<uint32_t>(asset.surfaces.size());
        report.surfaceLodRangeCount = static_cast<uint32_t>(asset.surfaceLodRanges.size());
        report.surfaceSectionCount = static_cast<uint32_t>(asset.surfaceSections.size());
        report.clusterCount = static_cast<uint32_t>(asset.clusters.size());
        report.pageCount = static_cast<uint32_t>(asset.pages.size());
        report.meshletPrimitiveCount = static_cast<uint32_t>(asset.meshletPrimitives.size());
        report.triangleCount = asset.totalTriangleCount;
        report.vertexCount = asset.totalVertexCount;
        report.maxVerticesPerCluster = RENDER3D::CLUSTER::CountMaxClusterVertices(asset);
        report.unsupportedFeatureCount = asset.unsupportedFeatureCount;
        uint64_t clusterTriangleTotal = 0;
        float cutoffMin = (std::numeric_limits<float>::max)();
        float cutoffMax = 0.0f;
        double cutoffSum = 0.0;
        uint32_t cutoffCount = 0;
        for (const MeshCluster& cluster : asset.clusters) {
            clusterTriangleTotal += cluster.triangleCount;
            report.maxTrianglesPerClusterObserved =
                (std::max)(report.maxTrianglesPerClusterObserved, cluster.triangleCount);
            if (cluster.triangleCount <= 1u) {
                ++report.singleTriangleClusterCount;
            }
            if (cluster.triangleCount < RENDER3D::CLUSTER::kHcmeshMaxTrianglesPerCluster / 4u) {
                ++report.lowTriangleClusterCount;
            }

            const float axisLength = MATH::Length(cluster.coneAxis);
            const bool axisValid = axisLength > 1.0e-5f;
            const bool cutoffValid =
                cluster.coneCutoff > 0.0f &&
                cluster.coneCutoff < 1.0f;
            if (axisValid && cutoffValid) {
                ++report.normalConeValidClusterCount;
                cutoffMin = (std::min)(cutoffMin, cluster.coneCutoff);
                cutoffMax = (std::max)(cutoffMax, cluster.coneCutoff);
                cutoffSum += cluster.coneCutoff;
                ++cutoffCount;
                continue;
            }

            ++report.normalConeInvalidClusterCount;
            if (!axisValid) {
                ++report.normalConeAxisInvalidCount;
            }
            if (cluster.coneCutoff <= 0.0f) {
                ++report.normalConeCutoffLeZeroCount;
            } else if (cluster.coneCutoff >= 1.0f) {
                ++report.normalConeCutoffGeOneCount;
            }
        }
        if (report.clusterCount > 0u) {
            report.averageTrianglesPerCluster =
                static_cast<float>(
                    static_cast<double>(clusterTriangleTotal) /
                    static_cast<double>(report.clusterCount));
        }
        if (cutoffCount > 0u) {
            report.normalConeCutoffMin = cutoffMin;
            report.normalConeCutoffAverage =
                static_cast<float>(cutoffSum / static_cast<double>(cutoffCount));
            report.normalConeCutoffMax = cutoffMax;
        }
    }

    void FillPackedGeometryByteReport(const ClusteredGeometryAsset& asset, ClusteredGeometryBuildReport& report) {
        report.fallbackIndexByteSize =
            static_cast<uint64_t>(asset.packedIndices.size()) * sizeof(uint16_t);
        report.meshletPrimitiveByteSize =
            static_cast<uint64_t>(asset.meshletPrimitives.size()) *
            sizeof(RENDER3D::CLUSTER::ClusterGeometryGpuMeshletPrimitive);
        report.packedVertexPositionByteSize =
            static_cast<uint64_t>(asset.packedVertices.size()) *
            sizeof(RENDER3D::CLUSTER::ClusterGeometryGpuVertexPosition);
        report.packedVertexAttributeByteSize =
            static_cast<uint64_t>(asset.packedVertices.size()) *
            sizeof(RENDER3D::CLUSTER::ClusterGeometryGpuVertexAttributes);
        report.packedSkinVertexByteSize =
            static_cast<uint64_t>(asset.packedSkinningVertices.size()) *
            sizeof(RENDER3D::CLUSTER::ClusterGeometryGpuSkinVertex);

        RENDER3D::CLUSTER::ClusterGeometryPackOptions packOptions{};
        packOptions.includeFallbackIndices = true;
        RENDER3D::CLUSTER::ClusterGeometryPackedBytes packed{};
        std::string packMessage{};
        if (!RENDER3D::CLUSTER::PackClusterGeometryForGpu(
                asset,
                packOptions,
                packed,
                &packMessage)) {
            report.messages.push_back(packMessage.empty()
                ? "cluster geometry GPU packing failed during report generation"
                : packMessage);
            return;
        }

        report.packedGeometryByteSize = static_cast<uint64_t>(packed.geometryBytes.size());
        report.packedMetadataByteSize = static_cast<uint64_t>(packed.metadataBytes.size());
        report.packedTotalByteSize =
            report.packedGeometryByteSize +
            report.packedMetadataByteSize;
    }

    void ApplyCookBudgetReport(
        const ClusteredGeometryCookSettings& settings,
        ClusteredGeometryBuildReport& report) {

        if (report.sourceStaticTriangleCount > 0u) {
            report.triangleInflationRatio =
                static_cast<float>(
                    static_cast<double>(report.triangleCount) /
                    static_cast<double>(report.sourceStaticTriangleCount));
            report.triangleBudgetExceeded =
                report.triangleInflationRatio > settings.maxTriangleInflationRatio;
            if (report.triangleBudgetExceeded) {
                report.messages.push_back(
                    "cluster geometry triangle inflation exceeds budget: ratio=" +
                    std::to_string(report.triangleInflationRatio) +
                    " budget=" +
                    std::to_string(settings.maxTriangleInflationRatio));
            }
        }

        if (report.sourceStaticVertexCount > 0u) {
            report.vertexInflationRatio =
                static_cast<float>(
                    static_cast<double>(report.vertexCount) /
                    static_cast<double>(report.sourceStaticVertexCount));
            report.vertexBudgetExceeded =
                report.vertexInflationRatio > settings.maxVertexInflationRatio;
            if (report.vertexBudgetExceeded) {
                report.messages.push_back(
                    "cluster geometry vertex inflation exceeds budget: ratio=" +
                    std::to_string(report.vertexInflationRatio) +
                    " budget=" +
                    std::to_string(settings.maxVertexInflationRatio));
            }
        }

        report.clusterOccupancyWarning =
            report.clusterCount > 0u &&
            report.averageTrianglesPerCluster < settings.minAverageTrianglesPerClusterWarning;
        if (report.clusterOccupancyWarning) {
            report.messages.push_back(
                "cluster geometry average triangles per cluster is below budget: average=" +
                std::to_string(report.averageTrianglesPerCluster) +
                " budget=" +
                std::to_string(settings.minAverageTrianglesPerClusterWarning));
        }
    }

} // namespace HIKARI::ASSETS::GEOMETRY::COOKING
