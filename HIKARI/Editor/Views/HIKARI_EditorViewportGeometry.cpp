#include "Editor/Views/HIKARI_EditorViewportGeometry.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace HIKARI::EDITOR {
namespace {
bool Unproject(const MATH::Mat4& inverseViewProjection, float x, float y, float z, MATH::Vec3& outPoint) noexcept {
    const MATH::Vec4 world = inverseViewProjection.TransformPoint({x, y, z, 1.0f});
    if (!MATH::IsFinite(world) || std::abs(world.w) <= 1.0e-6f) {
        return false;
    }
    const float inverseW = 1.0f / world.w;
    outPoint = {world.x * inverseW, world.y * inverseW, world.z * inverseW};
    return MATH::IsFinite(outPoint);
}

} // namespace
bool IntersectEditorBounds(const EditorPointerRay& ray, const MATH::Vec3& minimum, const MATH::Vec3& maximum,
                           float& outDistance) noexcept {
    float nearDistance = 0.0f;
    float farDistance = (std::numeric_limits<float>::max)();
    const float origins[]{ray.origin.x, ray.origin.y, ray.origin.z};
    const float directions[]{ray.direction.x, ray.direction.y, ray.direction.z};
    const float minimums[]{minimum.x, minimum.y, minimum.z};
    const float maximums[]{maximum.x, maximum.y, maximum.z};
    for (size_t axis = 0u; axis < 3u; ++axis) {
        if (std::abs(directions[axis]) <= 1.0e-7f) {
            if (origins[axis] < minimums[axis] || origins[axis] > maximums[axis]) {
                return false;
            }
            continue;
        }
        const float inverseDirection = 1.0f / directions[axis];
        float a = (minimums[axis] - origins[axis]) * inverseDirection;
        float b = (maximums[axis] - origins[axis]) * inverseDirection;
        if (a > b) {
            std::swap(a, b);
        }
        nearDistance = (std::max)(nearDistance, a);
        farDistance = (std::min)(farDistance, b);
        if (nearDistance > farDistance) {
            return false;
        }
    }
    outDistance = nearDistance;
    return true;
}

bool BuildEditorPointerRay(const Camera3D& camera, float normalizedViewportX, float normalizedViewportY,
                           EditorPointerRay& outRay) noexcept {
    const float clipX = normalizedViewportX * 2.0f - 1.0f;
    const float clipY = 1.0f - normalizedViewportY * 2.0f;
    const MATH::Mat4 inverseViewProjection = MATH::Inverse(camera.GetViewProj());
    MATH::Vec3 nearPoint{};
    MATH::Vec3 farPoint{};
    if (!Unproject(inverseViewProjection, clipX, clipY, 0.0f, nearPoint) ||
        !Unproject(inverseViewProjection, clipX, clipY, 1.0f, farPoint)) {
        return false;
    }
    const MATH::Vec3 direction = farPoint - nearPoint;
    if (MATH::Length(direction) <= 1.0e-6f) {
        return false;
    }
    outRay.origin = nearPoint;
    outRay.direction = MATH::Normalize(direction);
    return true;
}

} // namespace HIKARI::EDITOR
