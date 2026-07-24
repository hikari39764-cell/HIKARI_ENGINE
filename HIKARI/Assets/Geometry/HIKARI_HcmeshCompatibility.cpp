#include "Assets/Geometry/HIKARI_HcmeshCompatibility.h"

#include <cstring>
#include <limits>
#include <utility>

#include "Core/Serialization/Binary/HIKARI_BinaryBuffer.h"
#include "Render3D/Cluster/HIKARI_ClusterGpuData.h"
#include "Render3D/Cluster/HIKARI_ClusteredGeometryAsset.h"

namespace HIKARI::ASSETS::GEOMETRY::COMPATIBILITY {

    namespace {
        using SERIALIZATION::BINARY::BUFFER::OverwriteTrivial;
        using SERIALIZATION::BINARY::BUFFER::ReadTrivial;

        constexpr uint32_t kStaticGpuVersion = 11u;

        struct StaticGpuHeaderV11 {
            uint32_t magic = 0;
            uint32_t version = 0;
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

            MATH::Vec4 localBoundsMin{};
            MATH::Vec4 localBoundsMax{};
        };

        static_assert(sizeof(StaticGpuHeaderV11) == 128u);

        constexpr uint32_t kHeaderGrowth =
            sizeof(RENDER3D::CLUSTER::ClusterGeometryGpuHeader) - sizeof(StaticGpuHeaderV11);

        bool ShiftOffset(uint32_t source, uint32_t& destination) {
            if (source == 0u) {
                destination = 0u;
                return true;
            }
            if (source < sizeof(StaticGpuHeaderV11) ||
                source > (std::numeric_limits<uint32_t>::max)() - kHeaderGrowth) {
                return false;
            }
            destination = source + kHeaderGrowth;
            return true;
        }

        bool UpgradeChunk(std::vector<uint8_t>& bytes, std::string& outMessage) {
            using RENDER3D::CLUSTER::ClusterGeometryGpuHeader;
            using RENDER3D::CLUSTER::ClusteredGeometryFlags;
            if (bytes.size() < sizeof(StaticGpuHeaderV11)) {
                outMessage = "[HCMESH] compatible GPU chunk is too small";
                return false;
            }

            StaticGpuHeaderV11 source{};
            size_t cursor = 0u;
            if (!ReadTrivial(
                std::span<const uint8_t>(bytes.data(), bytes.size()),
                cursor,
                source)) {
                outMessage =
                    "[HCMESH] compatible GPU chunk header is truncated";
                return false;
            }
            const uint32_t skinningFlag = static_cast<uint32_t>(ClusteredGeometryFlags::SkinningData);
            if (source.magic != RENDER3D::CLUSTER::kClusterGeometryGpuMagic ||
                source.version != kStaticGpuVersion ||
                source.byteSize != bytes.size() ||
                (source.flags & skinningFlag) != 0u) {
                outMessage = "[HCMESH] invalid compatible static GPU header";
                return false;
            }
            if (source.byteSize > (std::numeric_limits<uint32_t>::max)() - kHeaderGrowth) {
                outMessage = "[HCMESH] compatible GPU chunk is too large";
                return false;
            }

            ClusterGeometryGpuHeader upgraded{};
            upgraded.magic = source.magic;
            upgraded.version = RENDER3D::CLUSTER::kClusterGeometryGpuVersion;
            upgraded.flags = source.flags;
            upgraded.byteSize = source.byteSize + kHeaderGrowth;
            upgraded.surfaceCount = source.surfaceCount;
            upgraded.clusterCount = source.clusterCount;
            upgraded.pageCount = source.pageCount;
            upgraded.vertexCount = source.vertexCount;
            upgraded.indexCount = source.indexCount;
            upgraded.materialSlotCount = source.materialSlotCount;
            upgraded.totalTriangleCount = source.totalTriangleCount;
            upgraded.totalVertexCount = source.totalVertexCount;
            upgraded.meshletPrimitiveCount = source.meshletPrimitiveCount;
            upgraded.surfaceLodRangeCount = source.surfaceLodRangeCount;
            upgraded.surfaceSectionCount = source.surfaceSectionCount;
            upgraded.localBoundsMin = source.localBoundsMin;
            upgraded.localBoundsMax = source.localBoundsMax;

            if (!ShiftOffset(source.surfaceOffsetBytes, upgraded.surfaceOffsetBytes) ||
                !ShiftOffset(source.clusterOffsetBytes, upgraded.clusterOffsetBytes) ||
                !ShiftOffset(source.pageOffsetBytes, upgraded.pageOffsetBytes) ||
                !ShiftOffset(source.vertexOffsetBytes, upgraded.vertexOffsetBytes) ||
                !ShiftOffset(source.indexOffsetBytes, upgraded.indexOffsetBytes) ||
                !ShiftOffset(source.materialSlotOffsetBytes, upgraded.materialSlotOffsetBytes) ||
                !ShiftOffset(
                    source.meshletPrimitiveOffsetBytes,
                    upgraded.meshletPrimitiveOffsetBytes) ||
                !ShiftOffset(
                    source.surfaceLodRangeOffsetBytes,
                    upgraded.surfaceLodRangeOffsetBytes) ||
                !ShiftOffset(
                    source.surfaceSectionOffsetBytes,
                    upgraded.surfaceSectionOffsetBytes)) {
                outMessage = "[HCMESH] invalid compatible static GPU offsets";
                return false;
            }

            std::vector<uint8_t> upgradedBytes(upgraded.byteSize);
            if (!OverwriteTrivial(
                std::span<uint8_t>(
                    upgradedBytes.data(),
                    upgradedBytes.size()),
                0u,
                upgraded)) {
                outMessage =
                    "[HCMESH] failed to write upgraded GPU chunk header";
                return false;
            }
            std::memcpy(
                upgradedBytes.data() + sizeof(upgraded),
                bytes.data() + sizeof(source),
                bytes.size() - sizeof(source));
            bytes = std::move(upgradedBytes);
            return true;
        }
    } // namespace

    bool UpgradeStaticPackedGpuChunks(
        std::vector<uint8_t>& geometryBytes,
        std::vector<uint8_t>& metadataBytes,
        uint32_t& outHeaderGrowth,
        std::string& outMessage) {

        outHeaderGrowth = 0u;
        if (!UpgradeChunk(geometryBytes, outMessage) ||
            !UpgradeChunk(metadataBytes, outMessage)) {
            return false;
        }
        outHeaderGrowth = kHeaderGrowth;
        return true;
    }

} // namespace HIKARI::ASSETS::GEOMETRY::COMPATIBILITY
