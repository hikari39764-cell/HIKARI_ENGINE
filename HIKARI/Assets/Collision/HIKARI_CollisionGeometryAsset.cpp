#include "Assets/Collision/HIKARI_CollisionGeometryAsset.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::COLLISION {
    namespace {
        bool RangeFits(
            uint32_t offset,
            uint32_t count,
            size_t size) noexcept {

            return offset <= size && count <= size - offset;
        }

        bool IsShapeUsable(
            const CollisionGeometryAsset& asset,
            const CollisionGeometryShape& shape) noexcept {
            if (shape.id == 0u ||
                !MATH::IsFinite(shape.center) ||
                !MATH::IsFinite(shape.rotationEulerDegrees) ||
                !MATH::IsFinite(shape.size) ||
                !std::isfinite(shape.radius) ||
                !std::isfinite(shape.height)) {
                return false;
            }
            switch (shape.type) {
            case CollisionGeometryShapeType::Sphere:
                return shape.radius > 0.0f;
            case CollisionGeometryShapeType::Capsule:
                return shape.radius > 0.0f &&
                    shape.height >= shape.radius * 2.0f;
            case CollisionGeometryShapeType::ConvexHull:
                return shape.vertexCount >= 4u &&
                    RangeFits(
                        shape.vertexOffset,
                        shape.vertexCount,
                        asset.vertices.size()) &&
                    (shape.indexCount == 0u ||
                        (shape.indexCount % 3u == 0u &&
                         RangeFits(
                            shape.indexOffset,
                            shape.indexCount,
                            asset.indices.size())));
            case CollisionGeometryShapeType::TriangleMesh:
                return shape.vertexCount >= 3u &&
                    shape.indexCount >= 3u &&
                    shape.indexCount % 3u == 0u &&
                    RangeFits(
                        shape.vertexOffset,
                        shape.vertexCount,
                        asset.vertices.size()) &&
                    RangeFits(
                        shape.indexOffset,
                        shape.indexCount,
                        asset.indices.size());
            case CollisionGeometryShapeType::Box:
            default:
                return shape.size.x > 0.0f &&
                    shape.size.y > 0.0f &&
                    shape.size.z > 0.0f;
            }
        }
    }

    bool CollisionGeometryAsset::IsUsable() const noexcept {
        if (version != kCollisionGeometryAssetVersion ||
            sourceAssetGuid.empty() ||
            !BOUNDS::IsUsable(localBounds) ||
            shapes.empty()) {
            return false;
        }
        return std::all_of(
            shapes.begin(),
            shapes.end(),
            [this](const CollisionGeometryShape& shape) {
                return IsShapeUsable(*this, shape);
            });
    }

    uint32_t CollisionGeometryAsset::GetShapeCount() const noexcept {
        return static_cast<uint32_t>(shapes.size());
    }

    uint64_t CollisionGeometryAsset::GetPayloadByteSize() const noexcept {
        return static_cast<uint64_t>(shapes.size()) *
                sizeof(CollisionGeometryShape) +
            static_cast<uint64_t>(vertices.size()) * sizeof(MATH::Vec3) +
            static_cast<uint64_t>(indices.size()) * sizeof(uint32_t);
    }

    CollisionGeometryValidationResult ValidateCollisionGeometryAsset(
        const CollisionGeometryAsset& asset) {

        CollisionGeometryValidationResult result{};
        if (asset.version != kCollisionGeometryAssetVersion) {
            result.messages.push_back(
                "unsupported collision geometry version");
        }
        if (asset.sourceAssetGuid.empty()) {
            result.messages.push_back(
                "collision geometry has no source model GUID");
        }
        if (!BOUNDS::IsUsable(asset.localBounds)) {
            result.messages.push_back(
                "collision geometry bounds are invalid");
        }
        if (asset.shapes.empty()) {
            result.messages.push_back(
                "collision geometry has no shapes");
        }

        std::unordered_set<uint64_t> ids{};
        for (const CollisionGeometryShape& shape : asset.shapes) {
            if (!IsShapeUsable(asset, shape)) {
                result.messages.push_back(
                    "collision geometry contains an invalid shape");
                break;
            }
            const uint64_t vertexEnd =
                static_cast<uint64_t>(shape.vertexOffset) +
                shape.vertexCount;
            const uint64_t indexEnd =
                static_cast<uint64_t>(shape.indexOffset) +
                shape.indexCount;
            if (vertexEnd > asset.vertices.size() ||
                indexEnd > asset.indices.size()) {
                result.messages.push_back(
                    "collision geometry contains an invalid geometry range");
                break;
            }
            for (uint32_t index = 0u; index < shape.indexCount; ++index) {
                const uint32_t localIndex = asset.indices[
                    shape.indexOffset + index];
                if (localIndex >= shape.vertexCount) {
                    result.messages.push_back(
                        "collision geometry contains an out-of-range index");
                    break;
                }
            }
            if (!result.messages.empty()) {
                break;
            }
            if (!ids.insert(shape.id).second) {
                result.messages.push_back(
                    "collision geometry contains duplicate shape IDs");
                break;
            }
        }

        result.valid = result.messages.empty();
        return result;
    }

} // namespace HIKARI::ASSETS::COLLISION
