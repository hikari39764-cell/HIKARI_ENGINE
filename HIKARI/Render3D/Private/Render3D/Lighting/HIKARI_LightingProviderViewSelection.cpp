#include "Render3D/Lighting/HIKARI_LightingProviderViewSelection.h"

#include <algorithm>
#include <cstddef>
#include <cmath>

namespace HIKARI::RENDER3D::LIGHTING {

namespace {

MATH::Vec3 Subtract(const MATH::Vec3& lhs, const MATH::Vec3& rhs) noexcept {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

MATH::Vec3 AddScaled(const MATH::Vec3& origin, const MATH::Vec3& direction, float scale) noexcept {
    return {origin.x + direction.x * scale, origin.y + direction.y * scale, origin.z + direction.z * scale};
}

float DotLocal(const MATH::Vec3& lhs, const MATH::Vec3& rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

float SmoothWeight(float value) noexcept {
    const float x = std::clamp(value, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

float ClosestParameter(const LightingProviderViewSegment& view, const MATH::Vec3& point) noexcept {
    const MATH::Vec3 direction = Subtract(view.focusPosition, view.cameraPosition);
    const float lengthSquared = DotLocal(direction, direction);
    if (lengthSquared <= 1e-8f) {
        return 0.0f;
    }
    return std::clamp(DotLocal(Subtract(point, view.cameraPosition), direction) / lengthSquared, 0.0f, 1.0f);
}

float EvaluateBoxPointInfluence(const MATH::Vec3& point, const MATH::Vec3& center, const MATH::Vec3& halfSize,
                                float blendDistance) noexcept {
    const float edgeDistance =
        (std::min)({halfSize.x - std::abs(point.x - center.x), halfSize.y - std::abs(point.y - center.y),
                    halfSize.z - std::abs(point.z - center.z)});
    if (edgeDistance < 0.0f) {
        return 0.0f;
    }
    const float maximumBlend = (std::max)(0.001f, (std::min)({halfSize.x, halfSize.y, halfSize.z}));
    const float blend = std::clamp(blendDistance, 0.001f, maximumBlend);
    return SmoothWeight(edgeDistance / blend);
}

bool IntersectViewSegmentBox(const LightingProviderViewSegment& view, const MATH::Vec3& boxMinimum,
                             const MATH::Vec3& boxMaximum, float& outEnter, float& outExit) noexcept {
    const MATH::Vec3 direction = Subtract(view.focusPosition, view.cameraPosition);
    outEnter = 0.0f;
    outExit = 1.0f;
    const float origins[3] = {view.cameraPosition.x, view.cameraPosition.y, view.cameraPosition.z};
    const float directions[3] = {direction.x, direction.y, direction.z};
    const float minimum[3] = {boxMinimum.x, boxMinimum.y, boxMinimum.z};
    const float maximum[3] = {boxMaximum.x, boxMaximum.y, boxMaximum.z};
    for (size_t axis = 0; axis < 3u; ++axis) {
        if (std::abs(directions[axis]) <= 1e-8f) {
            if (origins[axis] < minimum[axis] || origins[axis] > maximum[axis]) {
                return false;
            }
            continue;
        }
        const float inverse = 1.0f / directions[axis];
        float first = (minimum[axis] - origins[axis]) * inverse;
        float second = (maximum[axis] - origins[axis]) * inverse;
        if (first > second) {
            std::swap(first, second);
        }
        outEnter = (std::max)(outEnter, first);
        outExit = (std::min)(outExit, second);
        if (outEnter > outExit) {
            return false;
        }
    }
    return outExit >= 0.0f && outEnter <= 1.0f;
}

} // namespace

float DistanceSquaredToViewSegment(const LightingProviderViewSegment& view, const MATH::Vec3& point) noexcept {
    const MATH::Vec3 direction = Subtract(view.focusPosition, view.cameraPosition);
    const MATH::Vec3 closest = AddScaled(view.cameraPosition, direction, ClosestParameter(view, point));
    const MATH::Vec3 delta = Subtract(point, closest);
    return DotLocal(delta, delta);
}

float EvaluateSphereInfluenceAlongView(const LightingProviderViewSegment& view, const MATH::Vec3& center, float radius,
                                       float blendDistance) noexcept {
    const float safeRadius = (std::max)(radius, 0.001f);
    const float distance = std::sqrt(DistanceSquaredToViewSegment(view, center));
    const float blend = std::clamp(blendDistance, 0.001f, safeRadius);
    return SmoothWeight((safeRadius - distance) / blend);
}

float EvaluateBoxInfluenceAlongView(const LightingProviderViewSegment& view, const MATH::Vec3& center,
                                    const MATH::Vec3& size, float blendDistance) noexcept {
    const MATH::Vec3 halfSize{(std::max)(size.x * 0.5f, 0.001f), (std::max)(size.y * 0.5f, 0.001f),
                              (std::max)(size.z * 0.5f, 0.001f)};
    const MATH::Vec3 boxMinimum{center.x - halfSize.x, center.y - halfSize.y, center.z - halfSize.z};
    const MATH::Vec3 boxMaximum{center.x + halfSize.x, center.y + halfSize.y, center.z + halfSize.z};
    float enter = 0.0f;
    float exit = 0.0f;
    if (!IntersectViewSegmentBox(view, boxMinimum, boxMaximum, enter, exit)) {
        return 0.0f;
    }

    const MATH::Vec3 direction = Subtract(view.focusPosition, view.cameraPosition);
    const float centerParameter = std::clamp(ClosestParameter(view, center), enter, exit);
    const float middleParameter = (enter + exit) * 0.5f;
    return (std::max)(EvaluateBoxPointInfluence(AddScaled(view.cameraPosition, direction, centerParameter), center,
                                                halfSize, blendDistance),
                      EvaluateBoxPointInfluence(AddScaled(view.cameraPosition, direction, middleParameter), center,
                                                halfSize, blendDistance));
}

} // namespace HIKARI::RENDER3D::LIGHTING
