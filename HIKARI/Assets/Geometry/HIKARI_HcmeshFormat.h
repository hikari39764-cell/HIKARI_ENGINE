#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Render3D/Cluster/HIKARI_ClusterGeometryPacked.h"
#include "Render3D/Cluster/HIKARI_ClusteredGeometryAsset.h"

namespace HIKARI::ASSETS::GEOMETRY {

    constexpr uint32_t kHcmeshMagic = 0x48434D48u; // HCMH
    // 15: cluster 寸法を 124tri/64vert に変更し、GPU blob v11 (position float3 /
    //     attributes 24B) を格納する。旧版ファイルは検証で invalid になり再 cook される。
    // 16 adds GPU blob v12's optional packed joint/weight stream. Version 15
    // static files remain readable through an in-memory header upgrade; new
    // files are always written as version 16.
    constexpr uint32_t kHcmeshVersion = 16u;
    constexpr uint32_t kHcmeshChunkAlignment = 16u;

    enum class HcmeshChunkKind : uint32_t {
        SourceInfo = 1u,
        GpuGeometry = 2u,
        GpuMetadata = 3u,
        SurfaceRanges = 4u,
        SurfaceLodRanges = 5u,
        SurfaceSections = 6u,
    };

    enum class HcmeshFormatFlags : uint32_t {
        None = 0u,
        GpuReadyChunks = 1u << 0,
        ContainsFallbackIndices = 1u << 1,
    };

    struct HcmeshHeader {
        uint32_t magic = kHcmeshMagic;
        uint32_t version = kHcmeshVersion;
        uint32_t headerSize = 0;
        uint32_t chunkCount = 0;

        uint32_t flags = 0;
        uint32_t formatFlags = 0;
        uint32_t surfaceCount = 0;
        uint32_t surfaceLodRangeCount = 0;

        uint32_t surfaceSectionCount = 0;
        uint32_t clusterCount = 0;
        uint32_t pageCount = 0;
        uint32_t vertexCount = 0;

        uint32_t indexCount = 0;
        uint32_t meshletPrimitiveCount = 0;
        uint32_t materialSlotCount = 0;
        uint32_t geometryByteSize = 0;

        uint32_t metadataByteSize = 0;
        uint32_t totalTriangleCount = 0;
        uint32_t totalVertexCount = 0;
        uint32_t maxVerticesPerCluster = 0;

        MATH::Vec4 localBoundsMin{};
        MATH::Vec4 localBoundsMax{};
    };

    struct HcmeshChunkDesc {
        uint32_t kind = 0;
        uint32_t flags = 0;
        uint64_t offset = 0;
        uint64_t size = 0;
        uint32_t elementCount = 0;
        uint32_t elementStride = 0;
        uint32_t reserved0 = 0;
        uint32_t reserved1 = 0;
    };

    struct HcmeshFileInfo {
        bool valid = false;
        uint32_t version = 0;
        uint32_t flags = 0;
        uint32_t formatFlags = 0;
        uint32_t surfaceCount = 0;
        uint32_t surfaceLodRangeCount = 0;
        uint32_t surfaceSectionCount = 0;
        uint32_t clusterCount = 0;
        uint32_t pageCount = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
        uint32_t meshletPrimitiveCount = 0;
        uint32_t materialSlotCount = 0;
        uint32_t totalTriangleCount = 0;
        uint32_t totalVertexCount = 0;
        uint32_t maxVerticesPerCluster = 0;
        uint64_t geometryByteSize = 0;
        uint64_t metadataByteSize = 0;
        std::string sourceModelGuid{};
        std::string sourceModelPath{};
    };

    bool WriteHcmeshFile(
        const std::filesystem::path& path,
        const RENDER3D::CLUSTER::ClusteredGeometryAsset& asset,
        std::string& outMessage);

    bool ReadHcmeshPackedFile(
        const std::filesystem::path& path,
        RENDER3D::CLUSTER::ClusterGeometryPackedBytes& outPacked,
        std::string& outMessage);

    bool InspectHcmeshFile(
        const std::filesystem::path& path,
        HcmeshFileInfo& outInfo,
        std::string& outMessage);

    bool ReadHcmeshFile(
        const std::filesystem::path& path,
        RENDER3D::CLUSTER::ClusteredGeometryAsset& outAsset,
        std::string& outMessage);

} // namespace HIKARI::ASSETS::GEOMETRY
