#include "Scene/Debug/HIKARI_PhysicsShapeDebugDraw.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"

namespace HIKARI::DEBUG {
    namespace {
        constexpr int kCircleSegments = 24;

        MATH::Vec3 TransformPoint(
            const MATH::Mat4& matrix,
            const MATH::Vec3& point) {
            const MATH::Vec4 transformed = matrix.TransformPoint(
                { point.x, point.y, point.z, 1.0f });
            return { transformed.x, transformed.y, transformed.z };
        }

        MATH::Vec3 Multiply(
            const MATH::Vec3& left,
            const MATH::Vec3& right) noexcept {
            return {
                left.x * right.x,
                left.y * right.y,
                left.z * right.z
            };
        }

        void SubmitLine(
            const MATH::Mat4& matrix,
            const MATH::Vec3& from,
            const MATH::Vec3& to,
            unsigned int color,
            bool xray) {
            RENDERER3D::DEBUG::Line3D line{};
            line.from = TransformPoint(matrix, from);
            line.to = TransformPoint(matrix, to);
            line.rgba = color;
            line.depthMode = xray
                ? RENDERER3D::DEBUG::DebugDepthMode::XRay
                : RENDERER3D::DEBUG::DebugDepthMode::DepthTest;
            RENDERER3D::DEBUG::SubmitLine3D(line);
        }

        void SubmitCircle(
            const MATH::Mat4& matrix,
            int axis,
            float radius,
            float axisOffset,
            unsigned int color,
            bool xray) {
            constexpr float kTwoPi = 6.2831853071795864769f;
            for (int i = 0; i < kCircleSegments; ++i) {
                const float angleA = kTwoPi *
                    static_cast<float>(i) /
                    static_cast<float>(kCircleSegments);
                const float angleB = kTwoPi *
                    static_cast<float>(i + 1) /
                    static_cast<float>(kCircleSegments);
                MATH::Vec3 a{};
                MATH::Vec3 b{};
                const auto writePoint = [axis, radius, axisOffset](
                    float angle,
                    MATH::Vec3& point) {
                    const float c = std::cos(angle) * radius;
                    const float s = std::sin(angle) * radius;
                    if (axis == 0) {
                        point = { axisOffset, c, s };
                    } else if (axis == 1) {
                        point = { c, axisOffset, s };
                    } else {
                        point = { c, s, axisOffset };
                    }
                };
                writePoint(angleA, a);
                writePoint(angleB, b);
                SubmitLine(matrix, a, b, color, xray);
            }
        }

        void SubmitCapsuleArc(
            const MATH::Mat4& matrix,
            int radialAxis,
            float radius,
            float cylinderHalfHeight,
            float side,
            unsigned int color,
            bool xray) {
            constexpr float kPi = 3.14159265358979323846f;
            const int segments = kCircleSegments / 2;
            for (int i = 0; i < segments; ++i) {
                const float a0 = kPi * static_cast<float>(i) /
                    static_cast<float>(segments);
                const float a1 = kPi * static_cast<float>(i + 1) /
                    static_cast<float>(segments);
                const auto makePoint = [=](float angle) {
                    const float radial = std::cos(angle) * radius;
                    const float vertical = side *
                        (cylinderHalfHeight +
                            std::sin(angle) * radius);
                    return radialAxis == 0
                        ? MATH::Vec3{ radial, vertical, 0.0f }
                        : MATH::Vec3{ 0.0f, vertical, radial };
                };
                SubmitLine(
                    matrix,
                    makePoint(a0),
                    makePoint(a1),
                    color,
                    xray);
            }
        }

        void DrawCapsule(
            const MATH::Mat4& shapeWorld,
            float radius,
            float height,
            unsigned int color,
            bool xray) {
            const float cylinderHalfHeight = (std::max)(
                0.0f,
                height * 0.5f - radius);
            SubmitCircle(
                shapeWorld, 1, radius,
                cylinderHalfHeight, color, xray);
            SubmitCircle(
                shapeWorld, 1, radius,
                -cylinderHalfHeight, color, xray);
            for (const MATH::Vec3 radial :
                    std::array<MATH::Vec3, 4>{
                        MATH::Vec3{ radius, 0.0f, 0.0f },
                        MATH::Vec3{ -radius, 0.0f, 0.0f },
                        MATH::Vec3{ 0.0f, 0.0f, radius },
                        MATH::Vec3{ 0.0f, 0.0f, -radius } }) {
                SubmitLine(
                    shapeWorld,
                    radial + MATH::Vec3{
                        0.0f, -cylinderHalfHeight, 0.0f },
                    radial + MATH::Vec3{
                        0.0f, cylinderHalfHeight, 0.0f },
                    color,
                    xray);
            }
            SubmitCapsuleArc(
                shapeWorld, 0, radius,
                cylinderHalfHeight, 1.0f, color, xray);
            SubmitCapsuleArc(
                shapeWorld, 0, radius,
                cylinderHalfHeight, -1.0f, color, xray);
            SubmitCapsuleArc(
                shapeWorld, 1, radius,
                cylinderHalfHeight, 1.0f, color, xray);
            SubmitCapsuleArc(
                shapeWorld, 1, radius,
                cylinderHalfHeight, -1.0f, color, xray);
        }

        void DrawGeometry(
            const MATH::Mat4& shapeWorld,
            const PHYSICS::PhysicsShapeDesc& shape,
            unsigned int color,
            bool xray) {
            if (shape.geometry == nullptr || shape.vertexCount < 3u) {
                return;
            }
            const auto& vertices = shape.geometry->vertices;
            const auto& indices = shape.geometry->indices;
            const uint64_t vertexEnd =
                static_cast<uint64_t>(shape.vertexOffset) +
                shape.vertexCount;
            const uint64_t indexEnd =
                static_cast<uint64_t>(shape.indexOffset) +
                shape.indexCount;
            if (vertexEnd > vertices.size() || indexEnd > indices.size()) {
                return;
            }
            const uint32_t maximumLines = xray ? 20000u : 4000u;
            uint32_t lineCount = 0u;
            const auto submitEdge = [&](uint32_t first, uint32_t second) {
                if (lineCount >= maximumLines ||
                    first >= shape.vertexCount ||
                    second >= shape.vertexCount) {
                    return;
                }
                SubmitLine(
                    shapeWorld,
                    Multiply(
                        vertices[shape.vertexOffset + first],
                        shape.localScale),
                    Multiply(
                        vertices[shape.vertexOffset + second],
                        shape.localScale),
                    color,
                    xray);
                ++lineCount;
            };
            for (uint32_t offset = 0u;
                offset + 2u < shape.indexCount &&
                    lineCount < maximumLines;
                offset += 3u) {
                const uint32_t a = indices[shape.indexOffset + offset];
                const uint32_t b = indices[shape.indexOffset + offset + 1u];
                const uint32_t c = indices[shape.indexOffset + offset + 2u];
                submitEdge(a, b);
                submitEdge(b, c);
                submitEdge(c, a);
            }
        }
    }

    void DrawPhysicsShape(
        const MATH::Mat4& bodyWorld,
        const PHYSICS::PhysicsShapeDesc& shape,
        unsigned int color,
        bool xray) {
        const MATH::Mat4 shapeWorld = bodyWorld *
            MATH::Mat4::Translate(shape.localCenter) *
            MATH::Mat4::Rotate(shape.localRotation);
        switch (shape.type) {
        case PHYSICS::PhysicsShapeType::Box: {
            RENDERER3D::DEBUG::WireCube cube{};
            cube.transform.useExplicitMatrix = true;
            cube.transform.explicitMatrix = shapeWorld *
                MATH::Mat4::Scale(shape.halfExtents * 2.0f);
            cube.size = 1.0f;
            cube.rgba = color;
            cube.depthMode = xray
                ? RENDERER3D::DEBUG::DebugDepthMode::XRay
                : RENDERER3D::DEBUG::DebugDepthMode::DepthTest;
            RENDERER3D::DEBUG::SubmitWireCube(cube);
            break;
        }
        case PHYSICS::PhysicsShapeType::Sphere:
            SubmitCircle(
                shapeWorld, 0, shape.radius, 0.0f, color, xray);
            SubmitCircle(
                shapeWorld, 1, shape.radius, 0.0f, color, xray);
            SubmitCircle(
                shapeWorld, 2, shape.radius, 0.0f, color, xray);
            break;
        case PHYSICS::PhysicsShapeType::Capsule:
            DrawCapsule(
                shapeWorld,
                shape.radius,
                shape.height,
                color,
                xray);
            break;
        case PHYSICS::PhysicsShapeType::ConvexHull:
        case PHYSICS::PhysicsShapeType::TriangleMesh:
            DrawGeometry(shapeWorld, shape, color, xray);
            break;
        }
    }

} // namespace HIKARI::DEBUG
