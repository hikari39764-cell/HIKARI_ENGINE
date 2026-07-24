#include "Assets/Geometry/HIKARI_HcmeshFormat.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <type_traits>
#include <utility>

#include "Assets/Geometry/HIKARI_HcmeshCompatibility.h"
#include "Core/Serialization/Binary/HIKARI_BinaryBuffer.h"
#include "Core/Serialization/Binary/HIKARI_BinaryStream.h"

namespace HIKARI::ASSETS::GEOMETRY {

    namespace {
        constexpr uint32_t kMaxChunkCount = 32u;
        constexpr uint64_t kMaxChunkBytes = 2ull * 1024ull * 1024ull * 1024ull;
        constexpr uint32_t kMaxStringBytes = 16u * 1024u * 1024u;

        using SERIALIZATION::BINARY::BUFFER::AppendTrivial;
        using SERIALIZATION::BINARY::BUFFER::ReadTrivial;
        using SERIALIZATION::BINARY::STREAM::ReadTrivial;
        using SERIALIZATION::BINARY::STREAM::ReadTrivialArray;
        using SERIALIZATION::BINARY::STREAM::WriteTrivial;
        using SERIALIZATION::BINARY::STREAM::WriteTrivialArray;

        struct ChunkPayload {
            HcmeshChunkKind kind = HcmeshChunkKind::SourceInfo;
            uint32_t elementCount = 0;
            uint32_t elementStride = 0;
            std::vector<uint8_t> bytes{};
        };

        uint32_t ToBits(HcmeshFormatFlags value) {
            return static_cast<uint32_t>(value);
        }

        uint64_t AlignUp(uint64_t value, uint64_t alignment) {
            return (value + alignment - 1u) & ~(alignment - 1u);
        }

        void AppendString(std::vector<uint8_t>& bytes, const std::string& value) {
            const uint32_t size = static_cast<uint32_t>(
                (std::min)(value.size(), static_cast<size_t>(kMaxStringBytes)));
            AppendTrivial(bytes, size);
            const size_t oldSize = bytes.size();
            bytes.resize(oldSize + size);
            if (size > 0u) {
                std::memcpy(bytes.data() + oldSize, value.data(), size);
            }
        }

        bool ReadStringFromBytes(
            const std::vector<uint8_t>& bytes,
            size_t& cursor,
            std::string& value) {

            value.clear();
            uint32_t size = 0;
            if (!ReadTrivial(
                std::span<const uint8_t>(bytes.data(), bytes.size()),
                cursor,
                size)) {
                return false;
            }
            if (size > kMaxStringBytes || cursor + size > bytes.size()) {
                return false;
            }
            value.assign(
                reinterpret_cast<const char*>(bytes.data() + cursor),
                reinterpret_cast<const char*>(bytes.data() + cursor + size));
            cursor += size;
            return true;
        }

        template<class T>
        ChunkPayload MakePodVectorChunk(
            HcmeshChunkKind kind,
            const std::vector<T>& values) {

            static_assert(std::is_trivially_copyable_v<T>);
            ChunkPayload chunk{};
            chunk.kind = kind;
            chunk.elementCount = static_cast<uint32_t>(values.size());
            chunk.elementStride = sizeof(T);
            chunk.bytes.resize(values.size() * sizeof(T));
            if (!values.empty()) {
                std::memcpy(chunk.bytes.data(), values.data(), chunk.bytes.size());
            }
            return chunk;
        }

        ChunkPayload MakeByteChunk(HcmeshChunkKind kind, const std::vector<uint8_t>& bytes) {
            ChunkPayload chunk{};
            chunk.kind = kind;
            chunk.elementCount = static_cast<uint32_t>(bytes.size());
            chunk.elementStride = 1u;
            chunk.bytes = bytes;
            return chunk;
        }

        ChunkPayload MakeSourceInfoChunk(
            const RENDER3D::CLUSTER::ClusteredGeometryAsset& asset) {

            ChunkPayload chunk{};
            chunk.kind = HcmeshChunkKind::SourceInfo;
            AppendString(chunk.bytes, asset.sourceModelGuid.value);
            AppendString(chunk.bytes, asset.sourceModelPath);
            chunk.elementCount = static_cast<uint32_t>(chunk.bytes.size());
            chunk.elementStride = 1u;
            return chunk;
        }

        const HcmeshChunkDesc* FindChunk(
            const std::vector<HcmeshChunkDesc>& chunks,
            HcmeshChunkKind kind) {

            const uint32_t kindBits = static_cast<uint32_t>(kind);
            for (const HcmeshChunkDesc& chunk : chunks) {
                if (chunk.kind == kindBits) {
                    return &chunk;
                }
            }
            return nullptr;
        }

        bool ReadChunkBytes(
            std::ifstream& ifs,
            const HcmeshChunkDesc& chunk,
            std::vector<uint8_t>& outBytes) {

            if (chunk.size > kMaxChunkBytes) {
                return false;
            }
            outBytes.resize(static_cast<size_t>(chunk.size));
            ifs.seekg(static_cast<std::streamoff>(chunk.offset), std::ios::beg);
            if (!ifs.good()) {
                return false;
            }
            return ReadTrivialArray(
                ifs,
                outBytes.data(),
                outBytes.size());
        }

        template<class T>
        bool DecodePodVectorChunk(
            const HcmeshChunkDesc& desc,
            const std::vector<uint8_t>& bytes,
            std::vector<T>& outValues) {

            static_assert(std::is_trivially_copyable_v<T>);
            if (desc.elementStride != sizeof(T) ||
                desc.size != static_cast<uint64_t>(desc.elementCount) * sizeof(T)) {
                return false;
            }
            outValues.resize(desc.elementCount);
            if (!outValues.empty()) {
                std::memcpy(outValues.data(), bytes.data(), bytes.size());
            }
            return true;
        }

        bool ReadRequiredChunk(
            std::ifstream& ifs,
            const std::vector<HcmeshChunkDesc>& chunks,
            HcmeshChunkKind kind,
            std::vector<uint8_t>& outBytes) {

            const HcmeshChunkDesc* chunk = FindChunk(chunks, kind);
            return chunk != nullptr && ReadChunkBytes(ifs, *chunk, outBytes);
        }

        bool ReadSourceInfo(
            std::ifstream& ifs,
            const std::vector<HcmeshChunkDesc>& chunks,
            HcmeshFileInfo& info) {

            const HcmeshChunkDesc* sourceChunk = FindChunk(chunks, HcmeshChunkKind::SourceInfo);
            if (sourceChunk == nullptr) {
                return true;
            }

            std::vector<uint8_t> bytes{};
            if (!ReadChunkBytes(ifs, *sourceChunk, bytes)) {
                return false;
            }
            size_t cursor = 0;
            return ReadStringFromBytes(bytes, cursor, info.sourceModelGuid) &&
                ReadStringFromBytes(bytes, cursor, info.sourceModelPath);
        }

        bool ValidateHeader(const HcmeshHeader& header, std::string& outMessage) {
            if (header.magic != kHcmeshMagic) {
                outMessage = "[HCMESH] invalid magic";
                return false;
            }
            if (header.version != kHcmeshVersion &&
                header.version != COMPATIBILITY::kStaticContainerVersion) {
                outMessage = "[HCMESH] unsupported version; reimport asset";
                return false;
            }
            if (header.headerSize != sizeof(HcmeshHeader) ||
                header.chunkCount == 0u ||
                header.chunkCount > kMaxChunkCount) {
                outMessage = "[HCMESH] invalid header";
                return false;
            }
            if ((header.formatFlags & ToBits(HcmeshFormatFlags::GpuReadyChunks)) == 0u) {
                outMessage = "[HCMESH] missing GPU-ready chunk flag";
                return false;
            }
            return true;
        }

        bool ReadHeaderAndChunks(
            std::ifstream& ifs,
            HcmeshHeader& header,
            std::vector<HcmeshChunkDesc>& chunks,
            std::string& outMessage) {

            if (!ReadTrivial(ifs, header) || !ValidateHeader(header, outMessage)) {
                if (outMessage.empty()) {
                    outMessage = "[HCMESH] failed to read header";
                }
                return false;
            }

            chunks.resize(header.chunkCount);
            const uint64_t minimumChunkOffset = AlignUp(
                static_cast<uint64_t>(header.headerSize) +
                sizeof(HcmeshChunkDesc) * static_cast<uint64_t>(header.chunkCount),
                kHcmeshChunkAlignment);
            for (HcmeshChunkDesc& chunk : chunks) {
                if (!ReadTrivial(ifs, chunk)) {
                    outMessage = "[HCMESH] failed to read chunk table";
                    return false;
                }
                if (chunk.size > kMaxChunkBytes ||
                    chunk.offset < minimumChunkOffset ||
                    chunk.offset + chunk.size < chunk.offset) {
                    outMessage = "[HCMESH] invalid chunk range";
                    return false;
                }
            }
            return true;
        }

        void FillInfoFromHeader(const HcmeshHeader& header, HcmeshFileInfo& info) {
            info.valid = true;
            info.version = header.version;
            info.flags = header.flags;
            info.formatFlags = header.formatFlags;
            info.surfaceCount = header.surfaceCount;
            info.surfaceLodRangeCount = header.surfaceLodRangeCount;
            info.surfaceSectionCount = header.surfaceSectionCount;
            info.clusterCount = header.clusterCount;
            info.pageCount = header.pageCount;
            info.vertexCount = header.vertexCount;
            info.indexCount = header.indexCount;
            info.meshletPrimitiveCount = header.meshletPrimitiveCount;
            info.materialSlotCount = header.materialSlotCount;
            info.totalTriangleCount = header.totalTriangleCount;
            info.totalVertexCount = header.totalVertexCount;
            info.maxVerticesPerCluster = header.maxVerticesPerCluster;
            info.geometryByteSize = header.geometryByteSize;
            info.metadataByteSize = header.metadataByteSize;
        }

        bool DecodePackedLayout(
            const std::vector<uint8_t>& geometryBytes,
            const std::vector<uint8_t>& metadataBytes,
            RENDER3D::CLUSTER::ClusterGeometryPackedBytes& packed,
            std::string& outMessage) {

            using RENDER3D::CLUSTER::ClusterGeometryGpuHeader;
            if (geometryBytes.size() < sizeof(ClusterGeometryGpuHeader) ||
                metadataBytes.size() < sizeof(ClusterGeometryGpuHeader)) {
                outMessage = "[HCMESH] packed buffers are too small";
                return false;
            }

            ClusterGeometryGpuHeader geometryHeader{};
            ClusterGeometryGpuHeader metadataHeader{};
            size_t geometryCursor = 0u;
            size_t metadataCursor = 0u;
            if (!ReadTrivial(
                std::span<const uint8_t>(
                    geometryBytes.data(),
                    geometryBytes.size()),
                geometryCursor,
                geometryHeader) ||
                !ReadTrivial(
                    std::span<const uint8_t>(
                        metadataBytes.data(),
                        metadataBytes.size()),
                    metadataCursor,
                    metadataHeader)) {
                outMessage = "[HCMESH] packed buffer headers are truncated";
                return false;
            }
            if (geometryHeader.magic != RENDER3D::CLUSTER::kClusterGeometryGpuMagic ||
                metadataHeader.magic != RENDER3D::CLUSTER::kClusterGeometryGpuMagic ||
                geometryHeader.version != RENDER3D::CLUSTER::kClusterGeometryGpuVersion ||
                metadataHeader.version != RENDER3D::CLUSTER::kClusterGeometryGpuVersion) {
                outMessage = "[HCMESH] invalid packed GPU header";
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
            return true;
        }
    } // namespace

    bool WriteHcmeshFile(
        const std::filesystem::path& path,
        const RENDER3D::CLUSTER::ClusteredGeometryAsset& asset,
        std::string& outMessage) {

        RENDER3D::CLUSTER::ClusterGeometryPackedBytes packed{};
        RENDER3D::CLUSTER::ClusterGeometryPackOptions packOptions{};
        packOptions.includeFallbackIndices = true;
        if (!RENDER3D::CLUSTER::PackClusterGeometryForGpu(
                asset,
                packOptions,
                packed,
                &outMessage)) {
            return false;
        }

        std::vector<ChunkPayload> payloads{};
        payloads.push_back(MakeSourceInfoChunk(asset));
        payloads.push_back(MakeByteChunk(HcmeshChunkKind::GpuGeometry, packed.geometryBytes));
        payloads.push_back(MakeByteChunk(HcmeshChunkKind::GpuMetadata, packed.metadataBytes));
        payloads.push_back(MakePodVectorChunk(HcmeshChunkKind::SurfaceRanges, packed.surfaceRanges));
        payloads.push_back(MakePodVectorChunk(HcmeshChunkKind::SurfaceLodRanges, packed.surfaceLodRanges));
        payloads.push_back(MakePodVectorChunk(HcmeshChunkKind::SurfaceSections, packed.surfaceSections));

        HcmeshHeader header{};
        header.headerSize = sizeof(HcmeshHeader);
        header.chunkCount = static_cast<uint32_t>(payloads.size());
        header.flags = packed.layout.flags;
        header.formatFlags =
            ToBits(HcmeshFormatFlags::GpuReadyChunks) |
            ToBits(HcmeshFormatFlags::ContainsFallbackIndices);
        header.surfaceCount = packed.layout.surfaceCount;
        header.surfaceLodRangeCount = packed.layout.surfaceLodRangeCount;
        header.surfaceSectionCount = packed.layout.surfaceSectionCount;
        header.clusterCount = packed.layout.clusterCount;
        header.pageCount = packed.layout.pageCount;
        header.vertexCount = packed.layout.vertexCount;
        header.indexCount = packed.layout.indexCount;
        header.meshletPrimitiveCount = packed.layout.meshletPrimitiveCount;
        header.materialSlotCount = packed.layout.materialSlotCount;
        header.geometryByteSize = static_cast<uint32_t>(packed.geometryBytes.size());
        header.metadataByteSize = static_cast<uint32_t>(packed.metadataBytes.size());
        header.totalTriangleCount = packed.layout.totalTriangleCount;
        header.totalVertexCount = packed.layout.totalVertexCount;
        header.maxVerticesPerCluster = RENDER3D::CLUSTER::CountMaxClusterVertices(asset);
        header.localBoundsMin = packed.layout.localBoundsMin;
        header.localBoundsMax = packed.layout.localBoundsMax;

        std::vector<HcmeshChunkDesc> chunks(payloads.size());
        uint64_t offset = AlignUp(
            sizeof(HcmeshHeader) + sizeof(HcmeshChunkDesc) * payloads.size(),
            kHcmeshChunkAlignment);
        for (size_t i = 0; i < payloads.size(); ++i) {
            const ChunkPayload& payload = payloads[i];
            HcmeshChunkDesc& desc = chunks[i];
            desc.kind = static_cast<uint32_t>(payload.kind);
            desc.offset = offset;
            desc.size = payload.bytes.size();
            desc.elementCount = payload.elementCount;
            desc.elementStride = payload.elementStride;
            offset = AlignUp(offset + desc.size, kHcmeshChunkAlignment);
        }

        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            outMessage = "[HCMESH] failed to create output directory: " + ec.message();
            return false;
        }

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs.is_open()) {
            outMessage = "[HCMESH] failed to open for write: " + path.generic_string();
            return false;
        }

        if (!WriteTrivial(ofs, header)) {
            outMessage = "[HCMESH] failed while writing header: " + path.generic_string();
            return false;
        }
        for (const HcmeshChunkDesc& chunk : chunks) {
            if (!WriteTrivial(ofs, chunk)) {
                outMessage = "[HCMESH] failed while writing chunk table: " + path.generic_string();
                return false;
            }
        }

        const char zero = 0;
        for (size_t i = 0; i < payloads.size(); ++i) {
            const std::streamoff currentPos = static_cast<std::streamoff>(ofs.tellp());
            if (currentPos < 0) {
                outMessage = "[HCMESH] failed while seeking output stream: " + path.generic_string();
                return false;
            }
            const uint64_t current = static_cast<uint64_t>(currentPos);
            for (uint64_t pad = current; pad < chunks[i].offset; ++pad) {
                if (!WriteTrivial(ofs, zero)) {
                    outMessage =
                        "[HCMESH] failed while writing chunk alignment: " +
                        path.generic_string();
                    return false;
                }
            }
            if (!WriteTrivialArray(
                ofs,
                payloads[i].bytes.data(),
                payloads[i].bytes.size())) {
                outMessage =
                    "[HCMESH] failed while writing chunk payload: " +
                    path.generic_string();
                return false;
            }
            const std::streamoff afterPayloadPos = static_cast<std::streamoff>(ofs.tellp());
            if (afterPayloadPos < 0) {
                outMessage = "[HCMESH] failed while seeking output stream: " + path.generic_string();
                return false;
            }
            const uint64_t afterPayload = static_cast<uint64_t>(afterPayloadPos);
            const uint64_t alignedEnd = AlignUp(afterPayload, kHcmeshChunkAlignment);
            for (uint64_t pad = afterPayload; pad < alignedEnd; ++pad) {
                if (!WriteTrivial(ofs, zero)) {
                    outMessage =
                        "[HCMESH] failed while writing chunk alignment: " +
                        path.generic_string();
                    return false;
                }
            }
        }

        if (!ofs.good()) {
            outMessage = "[HCMESH] failed while writing: " + path.generic_string();
            return false;
        }

        outMessage =
            "[HCMESH] wrote GPU-ready v" + std::to_string(kHcmeshVersion) + " " +
            path.generic_string();
        return true;
    }

    bool ReadHcmeshPackedFile(
        const std::filesystem::path& path,
        RENDER3D::CLUSTER::ClusterGeometryPackedBytes& outPacked,
        std::string& outMessage) {

        outPacked = {};
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            outMessage = "[HCMESH] failed to open: " + path.generic_string();
            return false;
        }

        HcmeshHeader header{};
        std::vector<HcmeshChunkDesc> chunks{};
        if (!ReadHeaderAndChunks(ifs, header, chunks, outMessage)) {
            outMessage += ": " + path.generic_string();
            return false;
        }

        RENDER3D::CLUSTER::ClusterGeometryPackedBytes packed{};
        if (!ReadRequiredChunk(ifs, chunks, HcmeshChunkKind::GpuGeometry, packed.geometryBytes) ||
            !ReadRequiredChunk(ifs, chunks, HcmeshChunkKind::GpuMetadata, packed.metadataBytes)) {
            outMessage = "[HCMESH] missing packed GPU chunks: " + path.generic_string();
            return false;
        }

        const bool upgradedCompatibleStatic =
            header.version == COMPATIBILITY::kStaticContainerVersion;
        uint32_t compatibleHeaderGrowth = 0u;
        if (upgradedCompatibleStatic &&
            !COMPATIBILITY::UpgradeStaticPackedGpuChunks(
                packed.geometryBytes,
                packed.metadataBytes,
                compatibleHeaderGrowth,
                outMessage)) {
            outMessage += ": " + path.generic_string();
            return false;
        }

        const HcmeshChunkDesc* surfaceRangesChunk = FindChunk(chunks, HcmeshChunkKind::SurfaceRanges);
        const HcmeshChunkDesc* lodRangesChunk = FindChunk(chunks, HcmeshChunkKind::SurfaceLodRanges);
        const HcmeshChunkDesc* sectionsChunk = FindChunk(chunks, HcmeshChunkKind::SurfaceSections);
        std::vector<uint8_t> surfaceRangeBytes{};
        std::vector<uint8_t> lodRangeBytes{};
        std::vector<uint8_t> sectionBytes{};
        if (surfaceRangesChunk == nullptr ||
            lodRangesChunk == nullptr ||
            sectionsChunk == nullptr ||
            !ReadChunkBytes(ifs, *surfaceRangesChunk, surfaceRangeBytes) ||
            !ReadChunkBytes(ifs, *lodRangesChunk, lodRangeBytes) ||
            !ReadChunkBytes(ifs, *sectionsChunk, sectionBytes) ||
            !DecodePodVectorChunk(*surfaceRangesChunk, surfaceRangeBytes, packed.surfaceRanges) ||
            !DecodePodVectorChunk(*lodRangesChunk, lodRangeBytes, packed.surfaceLodRanges) ||
            !DecodePodVectorChunk(*sectionsChunk, sectionBytes, packed.surfaceSections)) {
            outMessage = "[HCMESH] invalid CPU range chunks: " + path.generic_string();
            return false;
        }

        if (!DecodePackedLayout(
                packed.geometryBytes,
                packed.metadataBytes,
                packed,
                outMessage)) {
            outMessage += ": " + path.generic_string();
            return false;
        }

        const uint64_t expectedGeometryByteSize =
            static_cast<uint64_t>(header.geometryByteSize) +
            compatibleHeaderGrowth;
        const uint64_t expectedMetadataByteSize =
            static_cast<uint64_t>(header.metadataByteSize) +
            compatibleHeaderGrowth;
        if (packed.layout.surfaceCount != header.surfaceCount ||
            packed.layout.surfaceLodRangeCount != header.surfaceLodRangeCount ||
            packed.layout.surfaceSectionCount != header.surfaceSectionCount ||
            packed.layout.clusterCount != header.clusterCount ||
            packed.layout.pageCount != header.pageCount ||
            packed.layout.vertexCount != header.vertexCount ||
            packed.layout.indexCount != header.indexCount ||
            packed.layout.meshletPrimitiveCount != header.meshletPrimitiveCount ||
            packed.layout.byteSize != expectedGeometryByteSize ||
            packed.metadataByteSize != expectedMetadataByteSize) {
            outMessage = "[HCMESH] packed layout/header mismatch: " + path.generic_string();
            return false;
        }

        outPacked = std::move(packed);
        outMessage =
            "[HCMESH] read GPU-ready v" + std::to_string(header.version) +
            (upgradedCompatibleStatic ? " (GPU v11 upgraded in memory) " : " ") +
            path.generic_string();
        return true;
    }

    bool InspectHcmeshFile(
        const std::filesystem::path& path,
        HcmeshFileInfo& outInfo,
        std::string& outMessage) {

        outInfo = {};
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            outMessage = "[HCMESH] failed to open: " + path.generic_string();
            return false;
        }

        HcmeshHeader header{};
        std::vector<HcmeshChunkDesc> chunks{};
        if (!ReadHeaderAndChunks(ifs, header, chunks, outMessage)) {
            outMessage += ": " + path.generic_string();
            return false;
        }

        if (FindChunk(chunks, HcmeshChunkKind::GpuGeometry) == nullptr ||
            FindChunk(chunks, HcmeshChunkKind::GpuMetadata) == nullptr ||
            FindChunk(chunks, HcmeshChunkKind::SurfaceRanges) == nullptr ||
            FindChunk(chunks, HcmeshChunkKind::SurfaceLodRanges) == nullptr ||
            FindChunk(chunks, HcmeshChunkKind::SurfaceSections) == nullptr) {
            outMessage = "[HCMESH] missing required chunks: " + path.generic_string();
            return false;
        }

        FillInfoFromHeader(header, outInfo);
        if (!ReadSourceInfo(ifs, chunks, outInfo)) {
            outMessage = "[HCMESH] invalid source info chunk: " + path.generic_string();
            return false;
        }

        outMessage =
            "[HCMESH] inspected GPU-ready v" + std::to_string(header.version) + " " +
            path.generic_string();
        return true;
    }

    bool ReadHcmeshFile(
        const std::filesystem::path& path,
        RENDER3D::CLUSTER::ClusteredGeometryAsset& outAsset,
        std::string& outMessage) {

        outAsset = {};
        outMessage =
            "[HCMESH] CPU asset read path is unavailable; use ReadHcmeshPackedFile: " +
            path.generic_string();
        return false;
    }

} // namespace HIKARI::ASSETS::GEOMETRY
