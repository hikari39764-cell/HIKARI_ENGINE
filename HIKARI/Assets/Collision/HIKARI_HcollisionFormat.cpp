#include "Assets/Collision/HIKARI_HcollisionFormat.h"

#include <array>
#include <fstream>
#include <limits>
#include <new>
#include <type_traits>

#include "Core/Serialization/Binary/HIKARI_BinaryStream.h"

namespace HIKARI::ASSETS::COLLISION {
    namespace {
        constexpr std::array<char, 8> kMagic{
            'H', 'C', 'O', 'L', 'L', 'I', 'S', 'N'
        };
        constexpr uint32_t kFormatVersion = 3u;
        constexpr uint32_t kEndianMarker = 0x01020304u;
        constexpr uint32_t kMaximumGuidBytes = 4096u;
        constexpr uint32_t kMaximumShapes = 1024u * 1024u;
        constexpr uint32_t kMaximumVertices = 64u * 1024u * 1024u;
        constexpr uint32_t kMaximumIndices = 192u * 1024u * 1024u;
        constexpr uint64_t kFnvOffset = 1469598103934665603ull;
        constexpr uint64_t kFnvPrime = 1099511628211ull;
        constexpr uint64_t kHeaderBytes = 68u;
        // id/type/reserved (16) + transform/size (36) + radius/height (8)
        // + four geometry ranges (16).
        constexpr uint64_t kSerializedShapeBytes = 76u;
        constexpr uint64_t kSerializedVertexBytes = 12u;
        constexpr uint64_t kSerializedIndexBytes = 4u;
        constexpr uint64_t kMaximumPayloadBytes = 2ull * 1024ull *
            1024ull * 1024ull;

        using SERIALIZATION::BINARY::STREAM::ReadTrivial;
        using SERIALIZATION::BINARY::STREAM::ReadTrivialArray;
        using SERIALIZATION::BINARY::STREAM::WriteTrivial;
        using SERIALIZATION::BINARY::STREAM::WriteTrivialArray;

        bool WriteVec3(std::ostream& stream, const MATH::Vec3& value) {
            return WriteTrivial(stream, value.x) &&
                WriteTrivial(stream, value.y) &&
                WriteTrivial(stream, value.z);
        }

        bool ReadVec3(std::istream& stream, MATH::Vec3& value) {
            return ReadTrivial(stream, value.x) &&
                ReadTrivial(stream, value.y) &&
                ReadTrivial(stream, value.z);
        }

        bool WriteBounds(std::ostream& stream, const Bounds& bounds) {
            return WriteVec3(stream, bounds.min) &&
                WriteVec3(stream, bounds.max);
        }

        bool ReadBounds(std::istream& stream, Bounds& bounds) {
            return ReadVec3(stream, bounds.min) &&
                ReadVec3(stream, bounds.max);
        }

        void HashBytes(
            uint64_t& hash,
            const void* bytes,
            size_t byteCount) noexcept {

            const auto* data = static_cast<const uint8_t*>(bytes);
            for (size_t index = 0u; index < byteCount; ++index) {
                hash ^= data[index];
                hash *= kFnvPrime;
            }
        }

        template<class TValue>
        void HashPod(uint64_t& hash, const TValue& value) noexcept {
            static_assert(std::is_trivially_copyable_v<TValue>);
            HashBytes(hash, &value, sizeof(value));
        }

        void HashVec3(uint64_t& hash, const MATH::Vec3& value) noexcept {
            HashPod(hash, value.x);
            HashPod(hash, value.y);
            HashPod(hash, value.z);
        }

        bool TryComputeExpectedFileSize(
            uint32_t guidBytes,
            uint32_t shapeCount,
            uint32_t vertexCount,
            uint32_t indexCount,
            uint64_t& outSize) noexcept {
            const uint64_t size = kHeaderBytes +
                static_cast<uint64_t>(guidBytes) +
                static_cast<uint64_t>(shapeCount) *
                    kSerializedShapeBytes +
                static_cast<uint64_t>(vertexCount) *
                    kSerializedVertexBytes +
                static_cast<uint64_t>(indexCount) *
                    kSerializedIndexBytes;
            if (size > kMaximumPayloadBytes) {
                return false;
            }
            outSize = size;
            return true;
        }

        bool WriteShape(
            std::ostream& stream,
            const CollisionGeometryShape& shape) {

            const uint8_t type = static_cast<uint8_t>(shape.type);
            const uint8_t reserved[7]{};
            return WriteTrivial(stream, shape.id) &&
                WriteTrivial(stream, type) &&
                WriteTrivial(stream, reserved) &&
                WriteVec3(stream, shape.center) &&
                WriteVec3(stream, shape.rotationEulerDegrees) &&
                WriteVec3(stream, shape.size) &&
                WriteTrivial(stream, shape.radius) &&
                WriteTrivial(stream, shape.height) &&
                WriteTrivial(stream, shape.vertexOffset) &&
                WriteTrivial(stream, shape.vertexCount) &&
                WriteTrivial(stream, shape.indexOffset) &&
                WriteTrivial(stream, shape.indexCount);
        }

        bool ReadShape(
            std::istream& stream,
            CollisionGeometryShape& shape) {

            uint8_t type = 0u;
            uint8_t reserved[7]{};
            if (!ReadTrivial(stream, shape.id) ||
                !ReadTrivial(stream, type) ||
                !ReadTrivial(stream, reserved) ||
                !ReadVec3(stream, shape.center) ||
                !ReadVec3(stream, shape.rotationEulerDegrees) ||
                !ReadVec3(stream, shape.size) ||
                !ReadTrivial(stream, shape.radius) ||
                !ReadTrivial(stream, shape.height) ||
                !ReadTrivial(stream, shape.vertexOffset) ||
                !ReadTrivial(stream, shape.vertexCount) ||
                !ReadTrivial(stream, shape.indexOffset) ||
                !ReadTrivial(stream, shape.indexCount) ||
                type > static_cast<uint8_t>(
                    CollisionGeometryShapeType::TriangleMesh)) {
                return false;
            }
            shape.type = static_cast<CollisionGeometryShapeType>(type);
            return true;
        }
    }

    uint64_t ComputeCollisionGeometryContentHash(
        const CollisionGeometryAsset& asset) noexcept {
        uint64_t hash = kFnvOffset;
        HashBytes(
            hash,
            asset.sourceAssetGuid.data(),
            asset.sourceAssetGuid.size());
        HashVec3(hash, asset.localBounds.min);
        HashVec3(hash, asset.localBounds.max);
        for (const CollisionGeometryShape& shape : asset.shapes) {
            HashPod(hash, shape.id);
            const uint8_t type = static_cast<uint8_t>(shape.type);
            HashPod(hash, type);
            HashVec3(hash, shape.center);
            HashVec3(hash, shape.rotationEulerDegrees);
            HashVec3(hash, shape.size);
            HashPod(hash, shape.radius);
            HashPod(hash, shape.height);
            HashPod(hash, shape.vertexOffset);
            HashPod(hash, shape.vertexCount);
            HashPod(hash, shape.indexOffset);
            HashPod(hash, shape.indexCount);
        }
        for (const MATH::Vec3& vertex : asset.vertices) {
            HashVec3(hash, vertex);
        }
        for (uint32_t index : asset.indices) {
            HashPod(hash, index);
        }
        return hash;
    }

    bool WriteHcollisionFile(
        const std::filesystem::path& path,
        const CollisionGeometryAsset& asset,
        std::string& outMessage) {

        const CollisionGeometryValidationResult validation =
            ValidateCollisionGeometryAsset(asset);
        if (!validation.valid) {
            outMessage = validation.messages.empty()
                ? "[HCOLLISION] asset validation failed"
                : "[HCOLLISION] " + validation.messages.front();
            return false;
        }
        if (asset.sourceAssetGuid.size() > kMaximumGuidBytes ||
            asset.shapes.size() > kMaximumShapes ||
            asset.vertices.size() > kMaximumVertices ||
            asset.indices.size() > kMaximumIndices ||
            asset.shapes.size() >
                (std::numeric_limits<uint32_t>::max)() ||
            asset.vertices.size() >
                (std::numeric_limits<uint32_t>::max)() ||
            asset.indices.size() >
                (std::numeric_limits<uint32_t>::max)()) {
            outMessage = "[HCOLLISION] asset is too large to serialize";
            return false;
        }

        std::error_code ec{};
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            outMessage =
                "[HCOLLISION] failed to create output directory: " +
                ec.message();
            return false;
        }
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            outMessage = "[HCOLLISION] failed to open output: " +
                path.generic_string();
            return false;
        }

        const uint32_t guidBytes = static_cast<uint32_t>(
            asset.sourceAssetGuid.size());
        const uint32_t shapeCount = static_cast<uint32_t>(
            asset.shapes.size());
        const uint32_t vertexCount = static_cast<uint32_t>(
            asset.vertices.size());
        const uint32_t indexCount = static_cast<uint32_t>(
            asset.indices.size());
        const uint64_t contentHash =
            ComputeCollisionGeometryContentHash(asset);

        bool ok = WriteTrivial(stream, kMagic) &&
            WriteTrivial(stream, kFormatVersion) &&
            WriteTrivial(stream, kEndianMarker) &&
            WriteTrivial(stream, asset.version) &&
            WriteTrivial(stream, guidBytes) &&
            WriteTrivial(stream, shapeCount) &&
            WriteTrivial(stream, vertexCount) &&
            WriteTrivial(stream, indexCount) &&
            WriteTrivial(stream, contentHash) &&
            WriteBounds(stream, asset.localBounds);
        ok = ok && WriteTrivialArray(
            stream,
            asset.sourceAssetGuid.data(),
            asset.sourceAssetGuid.size());
        for (const CollisionGeometryShape& shape : asset.shapes) {
            ok = ok && WriteShape(stream, shape);
        }
        for (const MATH::Vec3& vertex : asset.vertices) {
            ok = ok && WriteVec3(stream, vertex);
        }
        for (uint32_t index : asset.indices) {
            ok = ok && WriteTrivial(stream, index);
        }
        stream.flush();
        ok = ok && stream.good();
        if (!ok) {
            outMessage = "[HCOLLISION] failed while writing: " +
                path.generic_string();
            return false;
        }
        outMessage = "[HCOLLISION] wrote " + path.generic_string();
        return true;
    }

    bool ReadHcollisionFile(
        const std::filesystem::path& path,
        CollisionGeometryAsset& outAsset,
        std::string& outMessage,
        HcollisionReadInfo* outInfo) {

        outAsset = {};
        if (outInfo != nullptr) {
            *outInfo = {};
        }
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open()) {
            outMessage = "[HCOLLISION] failed to open: " +
                path.generic_string();
            return false;
        }

        stream.seekg(0, std::ios::end);
        const std::streamoff fileEnd = stream.tellg();
        if (fileEnd < 0) {
            outMessage = "[HCOLLISION] failed to inspect file size: " +
                path.generic_string();
            return false;
        }
        const uint64_t fileSize = static_cast<uint64_t>(fileEnd);
        stream.seekg(0, std::ios::beg);

        std::array<char, 8> magic{};
        uint32_t formatVersion = 0u;
        uint32_t endianMarker = 0u;
        uint32_t guidBytes = 0u;
        uint32_t shapeCount = 0u;
        uint32_t vertexCount = 0u;
        uint32_t indexCount = 0u;
        uint64_t storedHash = 0u;
        bool ok = ReadTrivial(stream, magic) && magic == kMagic &&
            ReadTrivial(stream, formatVersion) &&
            ReadTrivial(stream, endianMarker) &&
            ReadTrivial(stream, outAsset.version) &&
            ReadTrivial(stream, guidBytes) &&
            ReadTrivial(stream, shapeCount) &&
            ReadTrivial(stream, vertexCount) &&
            ReadTrivial(stream, indexCount) &&
            ReadTrivial(stream, storedHash) &&
            ReadBounds(stream, outAsset.localBounds);
        if (!ok ||
            formatVersion != kFormatVersion ||
            endianMarker != kEndianMarker ||
            outAsset.version != kCollisionGeometryAssetVersion ||
            guidBytes > kMaximumGuidBytes ||
            shapeCount > kMaximumShapes ||
            vertexCount > kMaximumVertices ||
            indexCount > kMaximumIndices) {
            outMessage =
                "[HCOLLISION] invalid or unsupported v3 file; recook the model collision asset: " +
                path.generic_string();
            outAsset = {};
            return false;
        }

        uint64_t expectedFileSize = 0u;
        if (!TryComputeExpectedFileSize(
                guidBytes,
                shapeCount,
                vertexCount,
                indexCount,
                expectedFileSize) ||
            fileSize != expectedFileSize) {
            outMessage =
                "[HCOLLISION] file size does not match its header: " +
                path.generic_string();
            outAsset = {};
            return false;
        }

        try {
            outAsset.sourceAssetGuid.resize(guidBytes);
            outAsset.shapes.resize(shapeCount);
            outAsset.vertices.resize(vertexCount);
            outAsset.indices.resize(indexCount);
        } catch (const std::bad_alloc&) {
            outMessage =
                "[HCOLLISION] payload allocation failed: " +
                path.generic_string();
            outAsset = {};
            return false;
        }
        ok = ok && ReadTrivialArray(
            stream,
            outAsset.sourceAssetGuid.data(),
            outAsset.sourceAssetGuid.size());
        for (CollisionGeometryShape& shape : outAsset.shapes) {
            ok = ok && ReadShape(stream, shape);
        }
        for (MATH::Vec3& vertex : outAsset.vertices) {
            ok = ok && ReadVec3(stream, vertex);
        }
        for (uint32_t& index : outAsset.indices) {
            ok = ok && ReadTrivial(stream, index);
        }
        if (!ok ||
            ComputeCollisionGeometryContentHash(outAsset) != storedHash) {
            outMessage =
                "[HCOLLISION] payload is truncated or corrupted: " +
                path.generic_string();
            outAsset = {};
            return false;
        }

        const CollisionGeometryValidationResult validation =
            ValidateCollisionGeometryAsset(outAsset);
        if (!validation.valid) {
            outMessage = validation.messages.empty()
                ? "[HCOLLISION] validation failed"
                : "[HCOLLISION] " + validation.messages.front();
            outAsset = {};
            return false;
        }
        if (outInfo != nullptr) {
            outInfo->contentHash = storedHash;
            outInfo->fileSize = fileSize;
        }
        outMessage = "[HCOLLISION] read " + path.generic_string();
        return true;
    }

} // namespace HIKARI::ASSETS::COLLISION
