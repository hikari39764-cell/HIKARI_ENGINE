#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI::ASSETS::COLLISION {

    constexpr uint32_t kCollisionGeometryAssetVersion = 3u;

    enum class CollisionGeometryShapeType : uint8_t {
        Box = 0u,
        Sphere = 1u,
        Capsule = 2u,
        ConvexHull = 3u,
        TriangleMesh = 4u,
    };

    struct CollisionGeometryShape {
        uint64_t id = 0u;
        CollisionGeometryShapeType type =
            CollisionGeometryShapeType::Box;
        MATH::Vec3 center{};
        MATH::Vec3 rotationEulerDegrees{};
        MATH::Vec3 size{ 1.0f, 1.0f, 1.0f };
        float radius = 0.5f;
        float height = 1.0f;
        uint32_t vertexOffset = 0u;
        uint32_t vertexCount = 0u;
        uint32_t indexOffset = 0u;
        uint32_t indexCount = 0u;
    };

    // Runtime-only collision artifact. Editable names and source-node metadata
    // live in ModelCollisionSetup and are intentionally not carried into the
    // physics-facing payload.
    struct CollisionGeometryAsset {
        uint32_t version = kCollisionGeometryAssetVersion;
        std::string sourceAssetGuid{};
        Bounds localBounds{};
        std::vector<CollisionGeometryShape> shapes{};
        std::vector<MATH::Vec3> vertices{};
        std::vector<uint32_t> indices{};

        bool IsUsable() const noexcept;
        uint32_t GetShapeCount() const noexcept;
        uint64_t GetPayloadByteSize() const noexcept;
    };

    struct CollisionGeometryValidationResult {
        bool valid = false;
        std::vector<std::string> messages{};
    };

    CollisionGeometryValidationResult ValidateCollisionGeometryAsset(
        const CollisionGeometryAsset& asset);

} // namespace HIKARI::ASSETS::COLLISION
