#include "Render3D/Cluster/HIKARI_ClusterGeometryPacked.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <DirectXPackedVector.h>

#include "Core/Numeric/HIKARI_IntegerConversion.h"
#include "Core/Serialization/Binary/HIKARI_BinaryBuffer.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::RENDER3D::CLUSTER {

    namespace {
        using SERIALIZATION::BINARY::BUFFER::AppendTrivial;
        using SERIALIZATION::BINARY::BUFFER::OverwriteTrivial;

        static_assert(
            kHcmeshMaxVerticesPerMeshlet <= 256u,
            "Packed meshlet primitive indices require 8-bit local vertex indices.");

        uint32_t AlignUp(uint32_t value, uint32_t alignment) {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }

        uint16_t PackSnorm16(float value) {
            const float clamped = std::clamp(value, -1.0f, 1.0f);
            const int32_t quantized =
                static_cast<int32_t>(std::round(clamped * 32767.0f));
            return static_cast<uint16_t>(
                static_cast<int16_t>(std::clamp(quantized, -32767, 32767)));
        }

        uint16_t PackHalf16(float value) {
            return static_cast<uint16_t>(DirectX::PackedVector::XMConvertFloatToHalf(value));
        }

        uint32_t PackPair16(uint16_t low, uint16_t high) {
            return static_cast<uint32_t>(low) | (static_cast<uint32_t>(high) << 16u);
        }

        MATH::Vec4 BoundsMin4(const Bounds& bounds) {
            return { bounds.min.x, bounds.min.y, bounds.min.z, 1.0f };
        }

        MATH::Vec4 BoundsMax4(const Bounds& bounds) {
            return { bounds.max.x, bounds.max.y, bounds.max.z, 1.0f };
        }

        const Bounds& ResolveLodMetricBounds(const ClusterSurfaceSection& section) {
            return BOUNDS::IsUsable(section.lodMetricBounds)
                ? section.lodMetricBounds
                : section.localBounds;
        }

        uint32_t AlignSection(std::vector<uint8_t>& bytes) {
            const uint32_t offset = NUMERIC::SaturateToUint32(bytes.size());
            const uint32_t alignedOffset = AlignUp(offset, kClusterGeometryGpuSectionAlignment);
            bytes.resize(alignedOffset);
            return alignedOffset;
        }
        // --- GPU 形式への変換関数群 ---
        ClusterGeometrySurfaceRange ToSurfaceRange(
            uint32_t surfaceIndex,
            const ClusterSurface& source) {

            ClusterGeometrySurfaceRange range{};
            range.surfaceIndex = surfaceIndex;
            range.nodeIndex = source.nodeIndex;
            range.meshIndex = source.meshIndex;
            range.primitiveIndex = source.primitiveIndex;
            range.materialIndex = source.materialIndex;
            range.firstCluster = source.firstCluster;
            range.clusterCount = source.clusterCount;
            range.flags = source.flags;
            range.firstIndex = source.firstIndex;
            range.indexCount = source.indexCount;
            range.firstVertex = source.firstVertex;
            range.vertexCount = source.vertexCount;
            range.firstPage = source.firstPage;
            range.pageCount = source.pageCount;
            range.firstPrimitive = source.firstPrimitive;
            range.primitiveCount = source.primitiveCount;
            range.firstLodRange = source.firstLodRange;
            range.lodRangeCount = source.lodRangeCount;
            return range;
        }

        ClusterGeometryGpuSurface ToGpuSurface(const ClusterSurface& source) {
            ClusterGeometryGpuSurface gpu{};
            gpu.nodeIndex = source.nodeIndex;
            gpu.meshIndex = source.meshIndex;
            gpu.primitiveIndex = source.primitiveIndex;
            gpu.materialIndex = source.materialIndex;
            gpu.firstCluster = source.firstCluster;
            gpu.clusterCount = source.clusterCount;
            gpu.firstIndex = source.firstIndex;
            gpu.indexCount = source.indexCount;
            gpu.firstVertex = source.firstVertex;
            gpu.vertexCount = source.vertexCount;
            gpu.flags = source.flags;
            gpu.firstPage = source.firstPage;
            gpu.pageCount = source.pageCount;
            gpu.firstPrimitive = source.firstPrimitive;
            gpu.primitiveCount = source.primitiveCount;
            gpu.firstLodRange = source.firstLodRange;
            gpu.lodRangeCount = source.lodRangeCount;
            gpu.firstSection = source.firstSection;
            gpu.sectionCount = source.sectionCount;
            gpu.boundsMin = BoundsMin4(source.localBounds);
            gpu.boundsMax = BoundsMax4(source.localBounds);
            return gpu;
        }

        template <typename Destination>
        Destination BuildSurfaceLodRangeRecord(
            const ClusterSurfaceLodRange& source) {

            Destination range{};
            range.surfaceIndex = source.surfaceIndex;
            range.lodIndex = source.lodIndex;
            range.firstCluster = source.firstCluster;
            range.clusterCount = source.clusterCount;
            range.firstIndex = source.firstIndex;
            range.indexCount = source.indexCount;
            range.firstVertex = source.firstVertex;
            range.vertexCount = source.vertexCount;
            range.firstPage = source.firstPage;
            range.pageCount = source.pageCount;
            range.firstPrimitive = source.firstPrimitive;
            range.primitiveCount = source.primitiveCount;
            range.geometricError = source.geometricError;
            range.minScreenRadius = source.minScreenRadius;
            range.flags = source.flags;
            range.sectionIndex = source.sectionIndex;
            return range;
        }

        template <typename Destination>
        Destination BuildSurfaceSectionRecord(
            const ClusterSurfaceSection& source) {

            Destination section{};
            section.surfaceIndex = source.surfaceIndex;
            section.sectionIndex = source.sectionIndex;
            section.firstCluster = source.firstCluster;
            section.clusterCount = source.clusterCount;
            section.firstIndex = source.firstIndex;
            section.indexCount = source.indexCount;
            section.firstVertex = source.firstVertex;
            section.vertexCount = source.vertexCount;
            section.firstPage = source.firstPage;
            section.pageCount = source.pageCount;
            section.firstPrimitive = source.firstPrimitive;
            section.primitiveCount = source.primitiveCount;
            section.firstLodRange = source.firstLodRange;
            section.lodRangeCount = source.lodRangeCount;
            section.flags = source.flags;
            section.boundsMin = BoundsMin4(source.localBounds);
            section.boundsMax = BoundsMax4(source.localBounds);
            section.lodMetricCenterRadius =
                BOUNDS::ComputeCenterRadius(ResolveLodMetricBounds(source));
            section.lodErrorBudgetNdc = source.lodErrorBudgetNdc;
            return section;
        }

        ClusterGeometryGpuCluster ToGpuCluster(const MeshCluster& source) {
            ClusterGeometryGpuCluster gpu{};
            gpu.surfaceIndex = source.surfaceIndex;
            gpu.firstIndex = source.firstIndex;
            gpu.indexCount = source.indexCount;
            gpu.firstVertex = source.firstVertex;
            gpu.vertexCount = source.vertexCount;
            gpu.triangleCount = source.triangleCount;
            gpu.flags = source.flags;
            gpu.firstPrimitive = source.firstPrimitive;
            gpu.boundsMin = BoundsMin4(source.localBounds);
            gpu.boundsMax = BoundsMax4(source.localBounds);
            gpu.sphereCenterRadius = {
                source.sphereCenter.x,
                source.sphereCenter.y,
                source.sphereCenter.z,
                source.sphereRadius
            };
            gpu.coneApex = {
                source.coneApex.x,
                source.coneApex.y,
                source.coneApex.z,
                source.coneReserved
            };
            gpu.coneAxisCutoff = {
                source.coneAxis.x,
                source.coneAxis.y,
                source.coneAxis.z,
                source.coneCutoff
            };
            return gpu;
        }

        ClusterGeometryGpuPage ToGpuPage(const ClusterPage& source) {
            ClusterGeometryGpuPage gpu{};
            gpu.firstCluster = source.firstCluster;
            gpu.clusterCount = source.clusterCount;
            gpu.firstIndex = source.firstIndex;
            gpu.indexCount = source.indexCount;
            gpu.firstVertex = source.firstVertex;
            gpu.vertexCount = source.vertexCount;
            gpu.firstPrimitive = source.firstPrimitive;
            gpu.primitiveCount = source.primitiveCount;
            gpu.boundsMin = BoundsMin4(source.localBounds);
            gpu.boundsMax = BoundsMax4(source.localBounds);
            return gpu;
        }

        ClusterGeometryGpuMeshletPrimitive ToGpuMeshletPrimitive(
            const MeshletPrimitive& source) {

            ClusterGeometryGpuMeshletPrimitive gpu{};
            gpu.packedIndices =
                (source.i0 & 0xffu) |
                ((source.i1 & 0xffu) << 8u) |
                ((source.i2 & 0xffu) << 16u) |
                ((source.reserved0 & 0xffu) << 24u);
            return gpu;
        }

        ClusterGeometryGpuVertexPosition ToGpuVertexPosition(const ClusterVertex& source) {
            ClusterGeometryGpuVertexPosition gpu{};
            gpu.position = { source.position.x, source.position.y, source.position.z };
            return gpu;
        }

        ClusterGeometryGpuVertexAttributes ToGpuVertexAttributes(const ClusterVertex& source) {
            ClusterGeometryGpuVertexAttributes gpu{};
            gpu.normalXY =
                PackPair16(PackSnorm16(source.normal.x), PackSnorm16(source.normal.y));
            gpu.normalZ_TangentW =
                PackPair16(PackSnorm16(source.normal.z), PackSnorm16(source.tangent.w));
            gpu.tangentXY =
                PackPair16(PackSnorm16(source.tangent.x), PackSnorm16(source.tangent.y));
            gpu.tangentZ_Uv0X =
                PackPair16(PackSnorm16(source.tangent.z), PackHalf16(source.uv0.x));
            gpu.uv0Y_Uv1X =
                PackPair16(PackHalf16(source.uv0.y), PackHalf16(source.uv1.x));
            gpu.uv1Y_Reserved0 =
                PackPair16(PackHalf16(source.uv1.y), 0u);
            return gpu;
        }

        ClusterGeometryGpuSkinVertex ToGpuSkinVertex(const ClusterSkinVertex& source) {
            ClusterGeometryGpuSkinVertex gpu{};
            gpu.joints01 = PackPair16(source.joints[0], source.joints[1]);
            gpu.joints23 = PackPair16(source.joints[2], source.joints[3]);
            gpu.weights01 = PackPair16(PackHalf16(source.weights[0]), PackHalf16(source.weights[1]));
            gpu.weights23 = PackPair16(PackHalf16(source.weights[2]), PackHalf16(source.weights[3]));
            return gpu;
        }
    } // namespace

    bool PackClusterGeometryForGpu(
        const ClusteredGeometryAsset& asset,
        const ClusterGeometryPackOptions& options,
        ClusterGeometryPackedBytes& outPacked,
        std::string* outMessage) {

        outPacked = {};
        if (!asset.valid ||
            asset.surfaces.empty() ||
            asset.clusters.empty() ||
            asset.packedVertices.empty() ||
            asset.meshletPrimitives.empty()) {
            if (outMessage != nullptr) {
                *outMessage = "[ClusterGeometryPack] invalid source asset";
            }
            return false;
        }
        if (options.includeFallbackIndices && asset.packedIndices.empty()) {
            if (outMessage != nullptr) {
                *outMessage = "[ClusterGeometryPack] fallback index data is required but missing";
            }
            return false;
        }
        const bool hasSkinningData =
            (asset.flags & static_cast<uint32_t>(ClusteredGeometryFlags::SkinningData)) != 0u;
        if (hasSkinningData && asset.packedSkinningVertices.size() != asset.packedVertices.size()) {
            if (outMessage != nullptr) {
                *outMessage = "[ClusterGeometryPack] skin stream must be parallel to the packed vertex stream";
            }
            return false;
        }

        ClusterGeometryPackedBytes packed{};
        packed.metadataBytes.resize(sizeof(ClusterGeometryGpuHeader));
        packed.geometryBytes.resize(sizeof(ClusterGeometryGpuHeader));

        ClusterGeometryGpuHeader metadataHeader{};
        metadataHeader.flags = asset.flags;
        metadataHeader.surfaceCount = NUMERIC::SaturateToUint32(asset.surfaces.size());
        metadataHeader.surfaceLodRangeCount = NUMERIC::SaturateToUint32(asset.surfaceLodRanges.size());
        metadataHeader.surfaceSectionCount = NUMERIC::SaturateToUint32(asset.surfaceSections.size());
        metadataHeader.clusterCount = NUMERIC::SaturateToUint32(asset.clusters.size());
        metadataHeader.pageCount = NUMERIC::SaturateToUint32(asset.pages.size());
        metadataHeader.vertexCount = NUMERIC::SaturateToUint32(asset.packedVertices.size());
        metadataHeader.skinVertexCount = hasSkinningData
            ? NUMERIC::SaturateToUint32(asset.packedSkinningVertices.size())
            : 0u;
        metadataHeader.indexCount = options.includeFallbackIndices
            ? NUMERIC::SaturateToUint32(asset.packedIndices.size())
            : 0u;
        metadataHeader.materialSlotCount = NUMERIC::SaturateToUint32(asset.materialSlotMapping.size());
        metadataHeader.meshletPrimitiveCount = NUMERIC::SaturateToUint32(asset.meshletPrimitives.size());
        metadataHeader.totalTriangleCount = asset.totalTriangleCount;
        metadataHeader.totalVertexCount = asset.totalVertexCount;
        metadataHeader.localBoundsMin = BoundsMin4(asset.localBounds);
        metadataHeader.localBoundsMax = BoundsMax4(asset.localBounds);

        ClusterGeometryGpuHeader geometryHeader = metadataHeader;

        metadataHeader.surfaceOffsetBytes = AlignSection(packed.metadataBytes);
        packed.surfaceRanges.reserve(asset.surfaces.size());
        for (size_t surfaceIndex = 0; surfaceIndex < asset.surfaces.size(); ++surfaceIndex) {
            const ClusterSurface& surface = asset.surfaces[surfaceIndex];
            AppendTrivial(packed.metadataBytes, ToGpuSurface(surface));
            packed.surfaceRanges.push_back(ToSurfaceRange(NUMERIC::SaturateToUint32(surfaceIndex), surface));
        }

        metadataHeader.surfaceLodRangeOffsetBytes = AlignSection(packed.metadataBytes);
        packed.surfaceLodRanges.reserve(asset.surfaceLodRanges.size());
        for (const ClusterSurfaceLodRange& lodRange : asset.surfaceLodRanges) {
            AppendTrivial(
                packed.metadataBytes,
                BuildSurfaceLodRangeRecord<ClusterGeometryGpuSurfaceLodRange>(
                    lodRange));
            packed.surfaceLodRanges.push_back(
                BuildSurfaceLodRangeRecord<ClusterGeometrySurfaceLodRange>(
                    lodRange));
        }

        metadataHeader.surfaceSectionOffsetBytes = AlignSection(packed.metadataBytes);
        packed.surfaceSections.reserve(asset.surfaceSections.size());
        for (const ClusterSurfaceSection& section : asset.surfaceSections) {
            AppendTrivial(
                packed.metadataBytes,
                BuildSurfaceSectionRecord<ClusterGeometryGpuSurfaceSection>(
                    section));
            packed.surfaceSections.push_back(
                BuildSurfaceSectionRecord<ClusterGeometrySurfaceSection>(
                    section));
        }

        metadataHeader.clusterOffsetBytes = AlignSection(packed.metadataBytes);
        for (const MeshCluster& cluster : asset.clusters) {
            AppendTrivial(packed.metadataBytes, ToGpuCluster(cluster));
        }

        metadataHeader.pageOffsetBytes = AlignSection(packed.metadataBytes);
        for (const ClusterPage& page : asset.pages) {
            AppendTrivial(packed.metadataBytes, ToGpuPage(page));
        }

        geometryHeader.vertexOffsetBytes = AlignSection(packed.geometryBytes);
        for (const ClusterVertex& vertex : asset.packedVertices) {
            AppendTrivial(packed.geometryBytes, ToGpuVertexPosition(vertex));
        }

        // 属性列は position 列の直後に隙間なく続ける。HLSL 側は
        // vertexOffsetBytes + vertexCount * POSITION_BYTES で属性先頭を導出する
        // (HikariClusterVertexAttributeOffset) ため、ここに padding を入れてはならない。
        for (const ClusterVertex& vertex : asset.packedVertices) {
            AppendTrivial(packed.geometryBytes, ToGpuVertexAttributes(vertex));
        }

        if (hasSkinningData) {
            geometryHeader.skinVertexOffsetBytes = AlignSection(packed.geometryBytes);
            for (const ClusterSkinVertex& vertex : asset.packedSkinningVertices) {
                AppendTrivial(packed.geometryBytes, ToGpuSkinVertex(vertex));
            }
        }

        if (options.includeFallbackIndices) {
            geometryHeader.indexOffsetBytes = AlignSection(packed.geometryBytes);
            std::vector<uint16_t> gpuIndices(asset.packedIndices.size(), 0u);
            for (const MeshCluster& cluster : asset.clusters) {
                const uint32_t indexEnd = cluster.firstIndex + cluster.indexCount;
                if (indexEnd > asset.packedIndices.size()) {
                    continue;
                }
                for (uint32_t indexOffset = 0; indexOffset < cluster.indexCount; ++indexOffset) {
                    const uint32_t sourceIndex = cluster.firstIndex + indexOffset;
                    gpuIndices[sourceIndex] =
                        static_cast<uint16_t>(asset.packedIndices[sourceIndex] & 0xffffu);
                }
            }
            for (const uint16_t index : gpuIndices) {
                AppendTrivial(packed.geometryBytes, index);
            }
        }

        geometryHeader.meshletPrimitiveOffsetBytes = AlignSection(packed.geometryBytes);
        for (const MeshletPrimitive& primitive : asset.meshletPrimitives) {
            AppendTrivial(packed.geometryBytes, ToGpuMeshletPrimitive(primitive));
        }

        geometryHeader.materialSlotOffsetBytes = AlignSection(packed.geometryBytes);
        for (const uint32_t materialSlot : asset.materialSlotMapping) {
            AppendTrivial(packed.geometryBytes, materialSlot);
        }

        packed.metadataBytes.resize(AlignUp(
            NUMERIC::SaturateToUint32(packed.metadataBytes.size()),
            kClusterGeometryGpuSectionAlignment));
        packed.geometryBytes.resize(AlignUp(
            NUMERIC::SaturateToUint32(packed.geometryBytes.size()),
            kClusterGeometryGpuSectionAlignment));

        metadataHeader.vertexOffsetBytes = geometryHeader.vertexOffsetBytes;
        metadataHeader.skinVertexCount = geometryHeader.skinVertexCount;
        metadataHeader.skinVertexOffsetBytes = geometryHeader.skinVertexOffsetBytes;
        metadataHeader.indexOffsetBytes = geometryHeader.indexOffsetBytes;
        metadataHeader.materialSlotOffsetBytes = geometryHeader.materialSlotOffsetBytes;
        metadataHeader.meshletPrimitiveOffsetBytes = geometryHeader.meshletPrimitiveOffsetBytes;
        metadataHeader.byteSize = NUMERIC::SaturateToUint32(packed.metadataBytes.size());

        geometryHeader.surfaceOffsetBytes = metadataHeader.surfaceOffsetBytes;
        geometryHeader.surfaceLodRangeOffsetBytes = metadataHeader.surfaceLodRangeOffsetBytes;
        geometryHeader.surfaceSectionOffsetBytes = metadataHeader.surfaceSectionOffsetBytes;
        geometryHeader.clusterOffsetBytes = metadataHeader.clusterOffsetBytes;
        geometryHeader.pageOffsetBytes = metadataHeader.pageOffsetBytes;
        geometryHeader.byteSize = NUMERIC::SaturateToUint32(packed.geometryBytes.size());

        if (!OverwriteTrivial(
            std::span<uint8_t>(
                packed.metadataBytes.data(),
                packed.metadataBytes.size()),
            0u,
            metadataHeader) ||
            !OverwriteTrivial(
                std::span<uint8_t>(
                    packed.geometryBytes.data(),
                    packed.geometryBytes.size()),
                0u,
                geometryHeader)) {
            if (outMessage != nullptr) {
                *outMessage =
                    "[ClusterGeometryPack] failed to write packed headers";
            }
            return false;
        }

        packed.layout.surfaceCount = metadataHeader.surfaceCount;
        packed.layout.surfaceLodRangeCount = metadataHeader.surfaceLodRangeCount;
        packed.layout.surfaceSectionCount = metadataHeader.surfaceSectionCount;
        packed.layout.clusterCount = metadataHeader.clusterCount;
        packed.layout.pageCount = metadataHeader.pageCount;
        packed.layout.vertexCount = geometryHeader.vertexCount;
        packed.layout.skinVertexCount = geometryHeader.skinVertexCount;
        packed.layout.indexCount = geometryHeader.indexCount;
        packed.layout.materialSlotCount = geometryHeader.materialSlotCount;
        packed.layout.surfaceOffsetBytes = metadataHeader.surfaceOffsetBytes;
        packed.layout.surfaceLodRangeOffsetBytes = metadataHeader.surfaceLodRangeOffsetBytes;
        packed.layout.surfaceSectionOffsetBytes = metadataHeader.surfaceSectionOffsetBytes;
        packed.layout.clusterOffsetBytes = metadataHeader.clusterOffsetBytes;
        packed.layout.pageOffsetBytes = metadataHeader.pageOffsetBytes;
        packed.layout.vertexOffsetBytes = geometryHeader.vertexOffsetBytes;
        packed.layout.skinVertexOffsetBytes = geometryHeader.skinVertexOffsetBytes;
        packed.layout.indexOffsetBytes = geometryHeader.indexOffsetBytes;
        packed.layout.materialSlotOffsetBytes = geometryHeader.materialSlotOffsetBytes;
        packed.layout.meshletPrimitiveCount = geometryHeader.meshletPrimitiveCount;
        packed.layout.meshletPrimitiveOffsetBytes = geometryHeader.meshletPrimitiveOffsetBytes;
        packed.layout.totalTriangleCount = metadataHeader.totalTriangleCount;
        packed.layout.totalVertexCount = metadataHeader.totalVertexCount;
        packed.layout.flags = metadataHeader.flags;
        packed.layout.byteSize = geometryHeader.byteSize;
        packed.layout.localBoundsMin = metadataHeader.localBoundsMin;
        packed.layout.localBoundsMax = metadataHeader.localBoundsMax;
        packed.metadataByteSize = metadataHeader.byteSize;

        outPacked = std::move(packed);
        if (outMessage != nullptr) {
            *outMessage = "[ClusterGeometryPack] packed GPU-ready cluster geometry";
        }
        return true;
    }

} // namespace HIKARI::RENDER3D::CLUSTER
