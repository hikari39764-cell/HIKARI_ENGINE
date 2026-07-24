#include "Assets/Collision/HIKARI_CollisionPrimitiveFitter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace HIKARI::ASSETS::COLLISION {
    namespace {
        constexpr float kMinimumSize = 0.001f;

        struct PrincipalFrame {
            MATH::Vec3 center{};
            std::array<MATH::Vec3, 3> axes{
                MATH::Vec3{ 1.0f, 0.0f, 0.0f },
                MATH::Vec3{ 0.0f, 1.0f, 0.0f },
                MATH::Vec3{ 0.0f, 0.0f, 1.0f }
            };
        };

        MATH::Quat QuaternionFromAxes(
            const MATH::Vec3& x,
            const MATH::Vec3& y,
            const MATH::Vec3& z) noexcept {

            const float m00 = x.x;
            const float m01 = y.x;
            const float m02 = z.x;
            const float m10 = x.y;
            const float m11 = y.y;
            const float m12 = z.y;
            const float m20 = x.z;
            const float m21 = y.z;
            const float m22 = z.z;
            MATH::Quat result{};
            const float trace = m00 + m11 + m22;
            if (trace > 0.0f) {
                const float scale = std::sqrt(trace + 1.0f) * 2.0f;
                result.w = 0.25f * scale;
                result.x = (m21 - m12) / scale;
                result.y = (m02 - m20) / scale;
                result.z = (m10 - m01) / scale;
            } else if (m00 > m11 && m00 > m22) {
                const float scale = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
                result.w = (m21 - m12) / scale;
                result.x = 0.25f * scale;
                result.y = (m01 + m10) / scale;
                result.z = (m02 + m20) / scale;
            } else if (m11 > m22) {
                const float scale = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
                result.w = (m02 - m20) / scale;
                result.x = (m01 + m10) / scale;
                result.y = 0.25f * scale;
                result.z = (m12 + m21) / scale;
            } else {
                const float scale = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
                result.w = (m10 - m01) / scale;
                result.x = (m02 + m20) / scale;
                result.y = (m12 + m21) / scale;
                result.z = 0.25f * scale;
            }
            return MATH::NormalizeQ(result);
        }

        PrincipalFrame ComputePrincipalFrame(
            const std::vector<MATH::Vec3>& vertices) {

            PrincipalFrame frame{};
            for (const MATH::Vec3& vertex : vertices) {
                frame.center = frame.center + vertex;
            }
            frame.center = frame.center *
                (1.0f / static_cast<float>(vertices.size()));

            double covariance[3][3]{};
            for (const MATH::Vec3& vertex : vertices) {
                const MATH::Vec3 delta = vertex - frame.center;
                const double values[3]{ delta.x, delta.y, delta.z };
                for (int row = 0; row < 3; ++row) {
                    for (int column = 0; column < 3; ++column) {
                        covariance[row][column] +=
                            values[row] * values[column];
                    }
                }
            }
            double eigenvectors[3][3]{
                { 1.0, 0.0, 0.0 },
                { 0.0, 1.0, 0.0 },
                { 0.0, 0.0, 1.0 }
            };
            for (int iteration = 0; iteration < 24; ++iteration) {
                int first = 0;
                int second = 1;
                double largest = std::abs(covariance[0][1]);
                if (std::abs(covariance[0][2]) > largest) {
                    first = 0;
                    second = 2;
                    largest = std::abs(covariance[0][2]);
                }
                if (std::abs(covariance[1][2]) > largest) {
                    first = 1;
                    second = 2;
                    largest = std::abs(covariance[1][2]);
                }
                if (largest <= 1.0e-12) {
                    break;
                }
                const double angle = 0.5 * std::atan2(
                    2.0 * covariance[first][second],
                    covariance[second][second] -
                        covariance[first][first]);
                const double cosine = std::cos(angle);
                const double sine = std::sin(angle);
                for (int index = 0; index < 3; ++index) {
                    const double firstValue = covariance[index][first];
                    const double secondValue = covariance[index][second];
                    covariance[index][first] =
                        cosine * firstValue - sine * secondValue;
                    covariance[index][second] =
                        sine * firstValue + cosine * secondValue;
                }
                for (int index = 0; index < 3; ++index) {
                    const double firstValue = covariance[first][index];
                    const double secondValue = covariance[second][index];
                    covariance[first][index] =
                        cosine * firstValue - sine * secondValue;
                    covariance[second][index] =
                        sine * firstValue + cosine * secondValue;
                }
                for (int index = 0; index < 3; ++index) {
                    const double firstValue = eigenvectors[index][first];
                    const double secondValue = eigenvectors[index][second];
                    eigenvectors[index][first] =
                        cosine * firstValue - sine * secondValue;
                    eigenvectors[index][second] =
                        sine * firstValue + cosine * secondValue;
                }
            }

            std::array<int, 3> order{ 0, 1, 2 };
            std::sort(
                order.begin(),
                order.end(),
                [&covariance](int left, int right) {
                    return covariance[left][left] > covariance[right][right];
                });
            for (int axisIndex = 0; axisIndex < 3; ++axisIndex) {
                const int source = order[axisIndex];
                frame.axes[axisIndex] = MATH::Normalize({
                    static_cast<float>(eigenvectors[0][source]),
                    static_cast<float>(eigenvectors[1][source]),
                    static_cast<float>(eigenvectors[2][source])
                });
            }
            frame.axes[2] = MATH::Normalize(MATH::Cross(
                frame.axes[0],
                frame.axes[1]));
            if (MATH::Length(frame.axes[2]) <= 1.0e-5f) {
                frame.axes = {
                    MATH::Vec3{ 1.0f, 0.0f, 0.0f },
                    MATH::Vec3{ 0.0f, 1.0f, 0.0f },
                    MATH::Vec3{ 0.0f, 0.0f, 1.0f }
                };
            } else {
                frame.axes[1] = MATH::Normalize(MATH::Cross(
                    frame.axes[2],
                    frame.axes[0]));
            }
            return frame;
        }

        void FitBox(
            const ModelCollisionMeshData& mesh,
            ModelCollisionShape& shape) {

            const PrincipalFrame frame = ComputePrincipalFrame(mesh.vertices);
            MATH::Vec3 minimum{
                (std::numeric_limits<float>::max)(),
                (std::numeric_limits<float>::max)(),
                (std::numeric_limits<float>::max)()
            };
            MATH::Vec3 maximum = minimum * -1.0f;
            for (const MATH::Vec3& vertex : mesh.vertices) {
                const MATH::Vec3 delta = vertex - frame.center;
                const MATH::Vec3 projected{
                    MATH::Dot(delta, frame.axes[0]),
                    MATH::Dot(delta, frame.axes[1]),
                    MATH::Dot(delta, frame.axes[2])
                };
                minimum.x = (std::min)(minimum.x, projected.x);
                minimum.y = (std::min)(minimum.y, projected.y);
                minimum.z = (std::min)(minimum.z, projected.z);
                maximum.x = (std::max)(maximum.x, projected.x);
                maximum.y = (std::max)(maximum.y, projected.y);
                maximum.z = (std::max)(maximum.z, projected.z);
            }
            const MATH::Vec3 localCenter = (minimum + maximum) * 0.5f;
            shape.center = frame.center +
                frame.axes[0] * localCenter.x +
                frame.axes[1] * localCenter.y +
                frame.axes[2] * localCenter.z;
            shape.size = maximum - minimum;
            shape.size.x = (std::max)(shape.size.x, kMinimumSize);
            shape.size.y = (std::max)(shape.size.y, kMinimumSize);
            shape.size.z = (std::max)(shape.size.z, kMinimumSize);
            shape.rotationEulerDegrees = MATH::EulerXYZDegreesFromQuat(
                QuaternionFromAxes(
                    frame.axes[0],
                    frame.axes[1],
                    frame.axes[2]));
        }

        void FitSphere(
            const ModelCollisionMeshData& mesh,
            ModelCollisionShape& shape) {

            std::array<size_t, 6> extreme{};
            for (size_t index = 1u; index < mesh.vertices.size(); ++index) {
                const MATH::Vec3& value = mesh.vertices[index];
                if (value.x < mesh.vertices[extreme[0]].x) extreme[0] = index;
                if (value.x > mesh.vertices[extreme[1]].x) extreme[1] = index;
                if (value.y < mesh.vertices[extreme[2]].y) extreme[2] = index;
                if (value.y > mesh.vertices[extreme[3]].y) extreme[3] = index;
                if (value.z < mesh.vertices[extreme[4]].z) extreme[4] = index;
                if (value.z > mesh.vertices[extreme[5]].z) extreme[5] = index;
            }
            size_t first = extreme[0];
            size_t second = extreme[1];
            float diameter = MATH::Length(
                mesh.vertices[second] - mesh.vertices[first]);
            for (int axis = 1; axis < 3; ++axis) {
                const float candidate = MATH::Length(
                    mesh.vertices[extreme[axis * 2 + 1]] -
                    mesh.vertices[extreme[axis * 2]]);
                if (candidate > diameter) {
                    diameter = candidate;
                    first = extreme[axis * 2];
                    second = extreme[axis * 2 + 1];
                }
            }
            shape.center = (mesh.vertices[first] + mesh.vertices[second]) * 0.5f;
            shape.radius = (std::max)(diameter * 0.5f, kMinimumSize);
            for (const MATH::Vec3& vertex : mesh.vertices) {
                const MATH::Vec3 delta = vertex - shape.center;
                const float distance = MATH::Length(delta);
                if (distance <= shape.radius) {
                    continue;
                }
                const float nextRadius = (shape.radius + distance) * 0.5f;
                shape.center = shape.center + delta *
                    ((nextRadius - shape.radius) / distance);
                shape.radius = nextRadius;
            }
        }

        void FitCapsule(
            const ModelCollisionMeshData& mesh,
            ModelCollisionShape& shape) {

            const PrincipalFrame frame = ComputePrincipalFrame(mesh.vertices);
            const MATH::Vec3 axis = frame.axes[0];
            float minimum = (std::numeric_limits<float>::max)();
            float maximum = -(std::numeric_limits<float>::max)();
            float radius = 0.0f;
            for (const MATH::Vec3& vertex : mesh.vertices) {
                const MATH::Vec3 delta = vertex - frame.center;
                const float projection = MATH::Dot(delta, axis);
                minimum = (std::min)(minimum, projection);
                maximum = (std::max)(maximum, projection);
                radius = (std::max)(
                    radius,
                    MATH::Length(delta - axis * projection));
            }
            shape.center = frame.center + axis * ((minimum + maximum) * 0.5f);
            shape.radius = (std::max)(radius, kMinimumSize);
            shape.height = (std::max)(
                maximum - minimum + shape.radius * 2.0f,
                shape.radius * 2.0f);
            const MATH::Vec3 helper = std::abs(axis.y) < 0.95f
                ? MATH::Vec3{ 0.0f, 1.0f, 0.0f }
                : MATH::Vec3{ 1.0f, 0.0f, 0.0f };
            MATH::Vec3 x = MATH::Normalize(MATH::Cross(helper, axis));
            MATH::Vec3 z = MATH::Normalize(MATH::Cross(x, axis));
            x = MATH::Normalize(MATH::Cross(axis, z));
            shape.rotationEulerDegrees = MATH::EulerXYZDegreesFromQuat(
                QuaternionFromAxes(x, axis, z));
        }
    }

    bool FitCollisionPrimitive(
        const ModelCollisionMeshData& mesh,
        CollisionGeometryShapeType type,
        ModelCollisionShape& outShape,
        float& outRelativeError,
        std::string& outMessage) {

        if (!mesh.IsUsable()) {
            outMessage = "primitive fitting requires usable mesh triangles";
            return false;
        }
        outShape.center = {};
        outShape.rotationEulerDegrees = {};
        outShape.size = { 1.0f, 1.0f, 1.0f };
        outShape.radius = 0.5f;
        outShape.height = 1.0f;
        switch (type) {
        case CollisionGeometryShapeType::Box:
            FitBox(mesh, outShape);
            break;
        case CollisionGeometryShapeType::Sphere:
            FitSphere(mesh, outShape);
            break;
        case CollisionGeometryShapeType::Capsule:
            FitCapsule(mesh, outShape);
            break;
        default:
            outMessage = "requested collision type is not a primitive";
            return false;
        }
        outRelativeError = 0.0f;
        outMessage.clear();
        return true;
    }

} // namespace HIKARI::ASSETS::COLLISION
