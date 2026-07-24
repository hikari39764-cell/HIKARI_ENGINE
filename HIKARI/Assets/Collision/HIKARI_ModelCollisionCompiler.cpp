#include "Assets/Collision/HIKARI_ModelCollisionCompiler.h"

#include <array>
#include <cmath>

#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::COLLISION {
    namespace {
        constexpr float kDegreesToRadians =
            0.01745329251994329577f;

        Bounds ShapeBounds(const ModelCollisionShape& shape) {
            if (shape.type == CollisionGeometryShapeType::ConvexHull ||
                shape.type == CollisionGeometryShapeType::TriangleMesh) {
                Bounds geometryBounds = BOUNDS::EmptyBounds();
                for (const MATH::Vec3& vertex : shape.vertices) {
                    BOUNDS::Encapsulate(geometryBounds, vertex);
                }
                if (!BOUNDS::IsUsable(geometryBounds)) {
                    return {};
                }
                const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(
                    shape.rotationEulerDegrees.x * kDegreesToRadians,
                    shape.rotationEulerDegrees.y * kDegreesToRadians,
                    shape.rotationEulerDegrees.z * kDegreesToRadians);
                return BOUNDS::TransformBounds(
                    geometryBounds,
                    MATH::Mat4::TRS(
                        shape.center,
                        rotation,
                        { 1.0f, 1.0f, 1.0f }));
            }
            MATH::Vec3 size = shape.size;
            if (shape.type == CollisionGeometryShapeType::Sphere) {
                size = {
                    shape.radius * 2.0f,
                    shape.radius * 2.0f,
                    shape.radius * 2.0f
                };
            } else if (shape.type ==
                    CollisionGeometryShapeType::Capsule) {
                size = {
                    shape.radius * 2.0f,
                    shape.height,
                    shape.radius * 2.0f
                };
            }
            const MATH::Vec3 half = size * 0.5f;
            const Bounds local{
                { -half.x, -half.y, -half.z },
                half
            };
            const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(
                shape.rotationEulerDegrees.x * kDegreesToRadians,
                shape.rotationEulerDegrees.y * kDegreesToRadians,
                shape.rotationEulerDegrees.z * kDegreesToRadians);
            return BOUNDS::TransformBounds(
                local,
                MATH::Mat4::TRS(
                    shape.center,
                    rotation,
                    { 1.0f, 1.0f, 1.0f }));
        }
    }

    ModelCollisionCompileResult CompileModelCollisionSetup(
        const ModelCollisionSetup& setup,
        CollisionGeometryAsset& outAsset) {

        outAsset = {};
        ModelCollisionCompileResult result{};
        if (!ValidateModelCollisionSetup(setup, result.message)) {
            return result;
        }
        if (setup.shapes.empty()) {
            result.message = "model collision setup has no shapes";
            return result;
        }

        outAsset.version = kCollisionGeometryAssetVersion;
        outAsset.sourceAssetGuid = setup.modelAssetGuid;
        Bounds bounds = BOUNDS::EmptyBounds();
        bool hasBounds = false;
        outAsset.shapes.reserve(setup.shapes.size());
        for (const ModelCollisionShape& source : setup.shapes) {
            if (!source.enabled) {
                continue;
            }
            CollisionGeometryShape shape{};
            shape.id = source.id;
            shape.type = source.type;
            shape.center = source.center;
            shape.rotationEulerDegrees = source.rotationEulerDegrees;
            shape.size = source.size;
            shape.radius = source.radius;
            shape.height = source.height;
            shape.vertexOffset = static_cast<uint32_t>(
                outAsset.vertices.size());
            shape.vertexCount = static_cast<uint32_t>(
                source.vertices.size());
            shape.indexOffset = static_cast<uint32_t>(
                outAsset.indices.size());
            shape.indexCount = static_cast<uint32_t>(
                source.indices.size());
            outAsset.vertices.insert(
                outAsset.vertices.end(),
                source.vertices.begin(),
                source.vertices.end());
            outAsset.indices.insert(
                outAsset.indices.end(),
                source.indices.begin(),
                source.indices.end());
            outAsset.shapes.push_back(shape);

            const Bounds shapeBounds = ShapeBounds(source);
            if (BOUNDS::IsUsable(shapeBounds)) {
                BOUNDS::Encapsulate(bounds, shapeBounds);
                hasBounds = true;
            }
        }
        outAsset.localBounds = hasBounds ? bounds : Bounds{};

        if (outAsset.shapes.empty()) {
            result.message = "model collision setup has no enabled shapes";
            outAsset = {};
            return result;
        }

        const CollisionGeometryValidationResult validation =
            ValidateCollisionGeometryAsset(outAsset);
        if (!validation.valid) {
            result.message = validation.messages.empty()
                ? "compiled HCOLLISION is invalid"
                : validation.messages.front();
            outAsset = {};
            return result;
        }
        result.success = true;
        result.shapeCount = outAsset.GetShapeCount();
        result.message = "compiled " +
            std::to_string(result.shapeCount) +
            " collision shape(s)";
        return result;
    }

} // namespace HIKARI::ASSETS::COLLISION
