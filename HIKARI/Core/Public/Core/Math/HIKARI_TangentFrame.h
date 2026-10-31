#pragma once

#include <cmath>
#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::MATH {
// HikariTransformSurfaceTangent と同じ world / handedness 契約。
inline Vec4 TransformSurfaceTangent(const Mat4& world, const Vec4& tangent) {
    const Vec4 transformed = world.TransformPoint({tangent.x, tangent.y, tangent.z, 0.0f});
    const Vec3 direction{transformed.x, transformed.y, transformed.z};
    const float lengthSquared = Dot(direction, direction);
    const Vec3 x{world.m[0][0], world.m[0][1], world.m[0][2]};
    const Vec3 y{world.m[1][0], world.m[1][1], world.m[1][2]};
    const Vec3 z{world.m[2][0], world.m[2][1], world.m[2][2]};
    const float orientation = Dot(x, Cross(y, z));
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1e-12f || !std::isfinite(orientation) ||
        !std::isfinite(tangent.w))
        return {0, 0, 0, 1};
    const Vec3 normalized = direction * (1.0f / std::sqrt(lengthSquared));
    return {normalized.x, normalized.y, normalized.z,
            (tangent.w < 0 ? -1.0f : 1.0f) * (orientation < 0 ? -1.0f : 1.0f)};
}

inline Vec3 BuildFallbackTangent(const Vec3& normal) {
    const Vec3 reference = std::abs(normal.y) < 0.999f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
    return Normalize(Cross(reference, normal));
}
} // namespace HIKARI::MATH
