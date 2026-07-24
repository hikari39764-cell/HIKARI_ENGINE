#pragma once

#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::CLUSTER {

    constexpr uint32_t kClusterGeometryGpuMagic = 0x534c4348u; // HCLS
    // 11: vertex position float4->float3 / attributes 32B->24B に縮小。
    // 12: optional packed joint/weight stream for mesh-shader skinning.
    constexpr uint32_t kClusterGeometryGpuVersion = 12u;
    constexpr uint32_t kClusterGeometryGpuSectionAlignment = 16u;

    struct ClusterGeometryGpuHeader {
        uint32_t magic = kClusterGeometryGpuMagic;
        uint32_t version = kClusterGeometryGpuVersion;
        uint32_t flags = 0;
        uint32_t byteSize = 0;

        uint32_t surfaceCount = 0;
        uint32_t clusterCount = 0;
        uint32_t pageCount = 0;
        uint32_t vertexCount = 0;

        uint32_t indexCount = 0;
        uint32_t materialSlotCount = 0;
        uint32_t totalTriangleCount = 0;
        uint32_t totalVertexCount = 0;

        uint32_t surfaceOffsetBytes = 0;
        uint32_t clusterOffsetBytes = 0;
        uint32_t pageOffsetBytes = 0;
        uint32_t vertexOffsetBytes = 0;

        uint32_t indexOffsetBytes = 0;
        uint32_t materialSlotOffsetBytes = 0;
        uint32_t meshletPrimitiveCount = 0;
        uint32_t meshletPrimitiveOffsetBytes = 0;

        uint32_t surfaceLodRangeCount = 0;
        uint32_t surfaceLodRangeOffsetBytes = 0;
        uint32_t surfaceSectionCount = 0;
        uint32_t surfaceSectionOffsetBytes = 0;

        uint32_t skinVertexCount = 0;
        uint32_t skinVertexOffsetBytes = 0;
        uint32_t reserved0 = 0;
        uint32_t reserved1 = 0;

        MATH::Vec4 localBoundsMin{};
        MATH::Vec4 localBoundsMax{};
    };

    struct ClusterGeometryGpuSurface {
        uint32_t nodeIndex = 0;
        uint32_t meshIndex = 0;
        uint32_t primitiveIndex = 0;
        uint32_t materialIndex = 0;

        uint32_t firstCluster = 0;
        uint32_t clusterCount = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;

        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        uint32_t flags = 0;
        uint32_t firstPage = 0;

        uint32_t pageCount = 0;
        uint32_t firstPrimitive = 0;
        uint32_t primitiveCount = 0;
        uint32_t firstLodRange = 0;

        uint32_t lodRangeCount = 0;
        uint32_t firstSection = 0;
        uint32_t sectionCount = 0;
        uint32_t reserved4 = 0;

        MATH::Vec4 boundsMin{};
        MATH::Vec4 boundsMax{};
    };

    struct ClusterGeometryGpuSurfaceLodRange {
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

    struct ClusterGeometryGpuSurfaceSection {
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
        uint32_t flags = 0;
        uint32_t reserved0 = 0;

        MATH::Vec4 boundsMin{};
        MATH::Vec4 boundsMax{};
        MATH::Vec4 lodMetricCenterRadius{};

        float lodErrorBudgetNdc = 0.006f;
        uint32_t reserved1 = 0;
        uint32_t reserved2 = 0;
        uint32_t reserved3 = 0;
    };

    struct ClusterGeometryGpuCluster {
        uint32_t surfaceIndex = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        uint32_t firstVertex = 0;

        uint32_t vertexCount = 0;
        uint32_t triangleCount = 0;
        uint32_t flags = 0;
        uint32_t firstPrimitive = 0;

        MATH::Vec4 boundsMin{};
        MATH::Vec4 boundsMax{};
        MATH::Vec4 sphereCenterRadius{};
        MATH::Vec4 coneApex{};
        MATH::Vec4 coneAxisCutoff{};
    };

    struct ClusterGeometryGpuPage {
        uint32_t firstCluster = 0;
        uint32_t clusterCount = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;

        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        uint32_t firstPrimitive = 0;
        uint32_t primitiveCount = 0;

        MATH::Vec4 boundsMin{};
        MATH::Vec4 boundsMax{};
    };

    struct ClusterGeometryGpuVertexPosition {
        MATH::Vec3 position{};
    };

    struct ClusterGeometryGpuVertexAttributes {
        uint32_t normalXY = 0;
        uint32_t normalZ_TangentW = 0;
        uint32_t tangentXY = 0;
        uint32_t tangentZ_Uv0X = 0;
        uint32_t uv0Y_Uv1X = 0;
        uint32_t uv1Y_Reserved0 = 0;
    };

    struct ClusterGeometryGpuSkinVertex {
        uint32_t joints01 = 0;
        uint32_t joints23 = 0;
        uint32_t weights01 = 0;
        uint32_t weights23 = 0;
    };

    struct ClusterGeometryGpuMeshletPrimitive {
        uint32_t packedIndices = 0;
    };

    struct ClusterGeometrySurfaceLodRange {
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

    struct ClusterGeometrySurfaceSection {
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
        uint32_t flags = 0;
        uint32_t reserved0 = 0;

        MATH::Vec4 boundsMin{};
        MATH::Vec4 boundsMax{};
        MATH::Vec4 lodMetricCenterRadius{};

        float lodErrorBudgetNdc = 0.006f;
        uint32_t reserved1 = 0;
        uint32_t reserved2 = 0;
        uint32_t reserved3 = 0;
    };

    struct ClusterGeometrySurfaceRange {
        uint32_t surfaceIndex = 0;
        uint32_t nodeIndex = 0;
        uint32_t meshIndex = 0;
        uint32_t primitiveIndex = 0;

        uint32_t materialIndex = 0;
        uint32_t firstCluster = 0;
        uint32_t clusterCount = 0;
        uint32_t flags = 0;

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
        uint32_t selectedLodIndex = 0;
        uint32_t reserved0 = 0;
    };

    static_assert(sizeof(ClusterGeometryGpuHeader) == 144u);
    static_assert((sizeof(ClusterGeometryGpuSurface) % kClusterGeometryGpuSectionAlignment) == 0);
    static_assert((sizeof(ClusterGeometryGpuSurfaceLodRange) % kClusterGeometryGpuSectionAlignment) == 0);
    static_assert((sizeof(ClusterGeometryGpuSurfaceSection) % kClusterGeometryGpuSectionAlignment) == 0);
    static_assert((sizeof(ClusterGeometrySurfaceLodRange) % kClusterGeometryGpuSectionAlignment) == 0);
    static_assert((sizeof(ClusterGeometrySurfaceSection) % kClusterGeometryGpuSectionAlignment) == 0);
    static_assert((sizeof(ClusterGeometryGpuCluster) % kClusterGeometryGpuSectionAlignment) == 0);
    static_assert((sizeof(ClusterGeometryGpuPage) % kClusterGeometryGpuSectionAlignment) == 0);
    // 頂点系はセクション先頭だけ 16B 境界に揃え、要素 stride は詰めて格納する。
    static_assert(sizeof(ClusterGeometryGpuVertexPosition) == 12);
    static_assert(sizeof(ClusterGeometryGpuVertexAttributes) == 24);
    static_assert(sizeof(ClusterGeometryGpuSkinVertex) == 16);
    static_assert(sizeof(ClusterGeometryGpuMeshletPrimitive) == sizeof(uint32_t));

} // namespace HIKARI::RENDER3D::CLUSTER
