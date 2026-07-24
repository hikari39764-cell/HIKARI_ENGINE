#include "Assets/Collision/HIKARI_ModelCollisionSetupGeometry.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <unordered_map>

#include "Core/IO/HIKARI_FileReplacementTransaction.h"
#include "Core/Serialization/Binary/HIKARI_BinaryStream.h"

namespace HIKARI::ASSETS::COLLISION {
    namespace {
        constexpr std::array<char, 8> kMagic{
            'H', 'C', 'S', 'E', 'T', 'G', 'E', 'O'
        };
        constexpr uint32_t kVersion = 1u;
        constexpr uint32_t kMaximumGeometryShapes = 65536u;
        constexpr uint32_t kMaximumVerticesPerShape = 16u * 1024u * 1024u;
        constexpr uint32_t kMaximumIndicesPerShape = 48u * 1024u * 1024u;

        using SERIALIZATION::BINARY::STREAM::ReadTrivial;
        using SERIALIZATION::BINARY::STREAM::WriteTrivial;

        bool HasGeometry(const ModelCollisionShape& shape) noexcept {
            return !shape.vertices.empty() || !shape.indices.empty();
        }

        bool RequiresGeometry(const ModelCollisionShape& shape) noexcept {
            return shape.type == CollisionGeometryShapeType::ConvexHull ||
                shape.type == CollisionGeometryShapeType::TriangleMesh;
        }
    }

    std::filesystem::path GetModelCollisionSetupGeometryPath(
        const std::filesystem::path& setupPath) {

        return std::filesystem::path(setupPath.string() + ".geometry.bin");
    }

    bool LoadModelCollisionSetupGeometry(
        const std::filesystem::path& setupPath,
        ModelCollisionSetup& setup,
        std::string& outMessage) {

        const bool geometryRequired = std::any_of(
            setup.shapes.begin(),
            setup.shapes.end(),
            RequiresGeometry);
        const std::filesystem::path path =
            GetModelCollisionSetupGeometryPath(setupPath);
        std::error_code ec{};
        if (!std::filesystem::exists(path, ec)) {
            if (geometryRequired) {
                outMessage = "collision setup geometry sidecar is missing";
                return false;
            }
            return true;
        }

        std::ifstream stream(path, std::ios::binary);
        std::array<char, 8> magic{};
        uint32_t version = 0u;
        uint32_t shapeCount = 0u;
        if (!ReadTrivial(stream, magic) || magic != kMagic ||
            !ReadTrivial(stream, version) || version != kVersion ||
            !ReadTrivial(stream, shapeCount) ||
            shapeCount > kMaximumGeometryShapes) {
            outMessage = "collision setup geometry sidecar is invalid";
            return false;
        }

        std::unordered_map<uint64_t, ModelCollisionShape*> byId{};
        for (ModelCollisionShape& shape : setup.shapes) {
            byId.emplace(shape.id, &shape);
        }
        for (uint32_t shapeIndex = 0u; shapeIndex < shapeCount; ++shapeIndex) {
            uint64_t id = 0u;
            uint32_t vertexCount = 0u;
            uint32_t indexCount = 0u;
            if (!ReadTrivial(stream, id) ||
                !ReadTrivial(stream, vertexCount) ||
                !ReadTrivial(stream, indexCount) ||
                vertexCount > kMaximumVerticesPerShape ||
                indexCount > kMaximumIndicesPerShape) {
                outMessage = "collision setup geometry sidecar is truncated";
                return false;
            }
            const auto found = byId.find(id);
            if (found == byId.end()) {
                outMessage = "collision setup geometry references an unknown shape";
                return false;
            }
            ModelCollisionShape& shape = *found->second;
            shape.vertices.resize(vertexCount);
            shape.indices.resize(indexCount);
            for (MATH::Vec3& vertex : shape.vertices) {
                if (!ReadTrivial(stream, vertex.x) ||
                    !ReadTrivial(stream, vertex.y) ||
                    !ReadTrivial(stream, vertex.z)) {
                    outMessage = "collision setup geometry vertices are truncated";
                    return false;
                }
            }
            for (uint32_t& index : shape.indices) {
                if (!ReadTrivial(stream, index)) {
                    outMessage = "collision setup geometry indices are truncated";
                    return false;
                }
            }
        }
        for (const ModelCollisionShape& shape : setup.shapes) {
            if (RequiresGeometry(shape) && shape.vertices.empty()) {
                outMessage = "collision setup geometry is incomplete";
                return false;
            }
        }
        return true;
    }

    bool SaveModelCollisionSetupGeometry(
        const std::filesystem::path& setupPath,
        const ModelCollisionSetup& setup,
        std::string& outMessage) {

        const std::filesystem::path path =
            GetModelCollisionSetupGeometryPath(setupPath);
        const uint32_t shapeCount = static_cast<uint32_t>(std::count_if(
            setup.shapes.begin(),
            setup.shapes.end(),
            HasGeometry));
        if (shapeCount == 0u) {
            std::error_code ec{};
            std::filesystem::remove(path, ec);
            return true;
        }

        const std::filesystem::path temporary(path.string() + ".tmp");
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) {
            outMessage = "failed to open collision setup geometry for write";
            return false;
        }
        bool ok = WriteTrivial(stream, kMagic) &&
            WriteTrivial(stream, kVersion) &&
            WriteTrivial(stream, shapeCount);
        for (const ModelCollisionShape& shape : setup.shapes) {
            if (!HasGeometry(shape)) {
                continue;
            }
            if (shape.vertices.size() > kMaximumVerticesPerShape ||
                shape.indices.size() > kMaximumIndicesPerShape ||
                shape.vertices.size() >
                    (std::numeric_limits<uint32_t>::max)() ||
                shape.indices.size() >
                    (std::numeric_limits<uint32_t>::max)()) {
                stream.close();
                std::filesystem::remove(temporary);
                outMessage = "collision setup geometry is too large";
                return false;
            }
            const uint32_t vertexCount = static_cast<uint32_t>(
                shape.vertices.size());
            const uint32_t indexCount = static_cast<uint32_t>(
                shape.indices.size());
            ok = ok && WriteTrivial(stream, shape.id) &&
                WriteTrivial(stream, vertexCount) &&
                WriteTrivial(stream, indexCount);
            for (const MATH::Vec3& vertex : shape.vertices) {
                ok = ok && WriteTrivial(stream, vertex.x) &&
                    WriteTrivial(stream, vertex.y) &&
                    WriteTrivial(stream, vertex.z);
            }
            for (uint32_t index : shape.indices) {
                ok = ok && WriteTrivial(stream, index);
            }
        }
        stream.flush();
        if (!ok || !stream.good()) {
            stream.close();
            std::filesystem::remove(temporary);
            outMessage = "failed while writing collision setup geometry";
            return false;
        }
        stream.close();

        std::string commitMessage{};
        if (!IO::CommitStagedFile(temporary, path, commitMessage)) {
            std::error_code cleanupEc{};
            std::filesystem::remove(temporary, cleanupEc);
            outMessage = "failed to replace collision setup geometry: " +
                commitMessage;
            return false;
        }
        return true;
    }

} // namespace HIKARI::ASSETS::COLLISION
