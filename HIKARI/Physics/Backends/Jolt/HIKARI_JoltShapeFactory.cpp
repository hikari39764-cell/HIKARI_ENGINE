#include "Physics/Backends/Jolt/HIKARI_JoltShapeFactory.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include "Physics/Backends/Jolt/HIKARI_JoltConversions.h"

namespace HIKARI::PHYSICS::JOLT_BACKEND {
    namespace {

        constexpr float kMinimumShapeSize = 0.001f;

        bool GeometryRangeIsValid(
            const PhysicsShapeDesc& shape) noexcept {

            if (!shape.geometry ||
                shape.vertexOffset > shape.geometry->vertices.size() ||
                shape.vertexCount >
                    shape.geometry->vertices.size() - shape.vertexOffset ||
                shape.indexOffset > shape.geometry->indices.size() ||
                shape.indexCount >
                    shape.geometry->indices.size() - shape.indexOffset) {
                return false;
            }
            return true;
        }

        JPH::ShapeRefC ApplyGeometryScale(
            const PhysicsShapeDesc& shape,
            JPH::ShapeRefC leaf) {

            if (!leaf) {
                return {};
            }
            const MATH::Vec3& scale = shape.localScale;
            if (std::abs(scale.x - 1.0f) <= 1.0e-6f &&
                std::abs(scale.y - 1.0f) <= 1.0e-6f &&
                std::abs(scale.z - 1.0f) <= 1.0e-6f) {
                return leaf;
            }
            JPH::ScaledShapeSettings scaled(
                leaf.GetPtr(),
                ToJolt(scale));
            scaled.mUserData = leaf->GetUserData();
            JPH::ShapeSettings::ShapeResult result = scaled.Create();
            return result.HasError()
                ? JPH::ShapeRefC{}
                : JPH::ShapeRefC(result.Get());
        }

        JPH::ShapeRefC BuildLeafShape(
            const PhysicsShapeDesc& shape,
            uint64_t userData,
            std::string* outError) {

            JPH::ShapeSettings::ShapeResult result;
            switch (shape.type) {
            case PhysicsShapeType::Box: {
                const JPH::Vec3 halfExtents(
                    (std::max)(shape.halfExtents.x, kMinimumShapeSize),
                    (std::max)(shape.halfExtents.y, kMinimumShapeSize),
                    (std::max)(shape.halfExtents.z, kMinimumShapeSize));
                JPH::BoxShapeSettings settings(halfExtents, 0.0f);
                settings.mDensity = (std::max)(
                    shape.material.density,
                    kMinimumShapeSize);
                settings.mUserData = userData;
                result = settings.Create();
                break;
            }
            case PhysicsShapeType::Sphere: {
                JPH::SphereShapeSettings settings(
                    (std::max)(shape.radius, kMinimumShapeSize));
                settings.mDensity = (std::max)(
                    shape.material.density,
                    kMinimumShapeSize);
                settings.mUserData = userData;
                result = settings.Create();
                break;
            }
            case PhysicsShapeType::Capsule: {
                const float radius = (std::max)(
                    shape.radius,
                    kMinimumShapeSize);
                const float halfCylinder = (std::max)(
                    0.0f,
                    shape.height * 0.5f - radius);
                JPH::CapsuleShapeSettings settings(
                    halfCylinder,
                    radius);
                settings.mDensity = (std::max)(
                    shape.material.density,
                    kMinimumShapeSize);
                settings.mUserData = userData;
                result = settings.Create();
                break;
            }
            case PhysicsShapeType::ConvexHull: {
                if (!GeometryRangeIsValid(shape) ||
                    shape.vertexCount < 4u) {
                    if (outError != nullptr) {
                        *outError = "convex hull geometry range is invalid";
                    }
                    return {};
                }
                JPH::Array<JPH::Vec3> points;
                points.reserve(shape.vertexCount);
                for (uint32_t index = 0u;
                    index < shape.vertexCount;
                    ++index) {
                    points.push_back(ToJolt(shape.geometry->vertices[
                        shape.vertexOffset + index]));
                }
                JPH::ConvexHullShapeSettings settings(points, 0.0f);
                settings.mDensity = (std::max)(
                    shape.material.density,
                    kMinimumShapeSize);
                settings.mUserData = userData;
                result = settings.Create();
                break;
            }
            case PhysicsShapeType::TriangleMesh: {
                if (!GeometryRangeIsValid(shape) ||
                    shape.vertexCount < 3u ||
                    shape.indexCount < 3u ||
                    shape.indexCount % 3u != 0u) {
                    if (outError != nullptr) {
                        *outError = "triangle mesh geometry range is invalid";
                    }
                    return {};
                }
                JPH::VertexList vertices;
                vertices.reserve(shape.vertexCount);
                for (uint32_t index = 0u;
                    index < shape.vertexCount;
                    ++index) {
                    const MATH::Vec3& vertex = shape.geometry->vertices[
                        shape.vertexOffset + index];
                    vertices.emplace_back(vertex.x, vertex.y, vertex.z);
                }
                JPH::IndexedTriangleList triangles;
                triangles.reserve(shape.indexCount / 3u);
                for (uint32_t index = 0u;
                    index < shape.indexCount;
                    index += 3u) {
                    const uint32_t first = shape.geometry->indices[
                        shape.indexOffset + index];
                    const uint32_t second = shape.geometry->indices[
                        shape.indexOffset + index + 1u];
                    const uint32_t third = shape.geometry->indices[
                        shape.indexOffset + index + 2u];
                    if (first >= shape.vertexCount ||
                        second >= shape.vertexCount ||
                        third >= shape.vertexCount) {
                        if (outError != nullptr) {
                            *outError = "triangle mesh contains an out-of-range index";
                        }
                        return {};
                    }
                    // Runtime filters decode sub-shape user data as the
                    // owning Collider slot. Every triangle in this mesh leaf
                    // must therefore retain the same leaf identity.
                    triangles.emplace_back(
                        first,
                        second,
                        third,
                        0u,
                        static_cast<uint32_t>(userData));
                }
                JPH::MeshShapeSettings settings(
                    std::move(vertices),
                    std::move(triangles));
                settings.mUserData = userData;
                result = settings.Create();
                break;
            }
            }
            JPH::ShapeRefC leaf = result.HasError()
                ? JPH::ShapeRefC{}
                : JPH::ShapeRefC(result.Get());
            if (!leaf && outError != nullptr && outError->empty()) {
                *outError = result.HasError()
                    ? result.GetError().c_str()
                    : "Jolt returned an empty collision shape";
            }
            if (shape.type == PhysicsShapeType::ConvexHull ||
                shape.type == PhysicsShapeType::TriangleMesh) {
                return ApplyGeometryScale(shape, std::move(leaf));
            }
            return leaf;
        }

    } // namespace

    JPH::ShapeRefC BuildCompoundShape(
        std::span<const PhysicsShapeDesc> shapes,
        std::string* outError) {

        if (shapes.empty()) {
            if (outError != nullptr) {
                *outError = "body has no collision shapes";
            }
            return {};
        }

        JPH::StaticCompoundShapeSettings compound;
        std::vector<JPH::ShapeRefC> leaves;
        leaves.reserve(shapes.size());

        for (size_t index = 0; index < shapes.size(); ++index) {
            JPH::ShapeRefC leaf = BuildLeafShape(
                shapes[index],
                static_cast<uint64_t>(index + 1u),
                outError);
            if (!leaf) {
                return {};
            }
            compound.AddShape(
                ToJolt(shapes[index].localCenter),
                ToJolt(shapes[index].localRotation),
                leaf.GetPtr());
            leaves.push_back(std::move(leaf));
        }

        JPH::ShapeSettings::ShapeResult result = compound.Create();
        if (result.HasError() && outError != nullptr) {
            *outError = result.GetError().c_str();
        }
        return result.HasError()
            ? JPH::ShapeRefC{}
            : JPH::ShapeRefC(result.Get());
    }

    JPH::ShapeRefC BuildQueryShape(
        const PhysicsShapeDesc& shape,
        std::string* outError) {
        if (shape.type == PhysicsShapeType::TriangleMesh) {
            if (outError != nullptr) {
                *outError = "triangle mesh query shapes are unsupported";
            }
            return {};
        }
        return BuildLeafShape(shape, 1u, outError);
    }

} // namespace HIKARI::PHYSICS::JOLT_BACKEND
