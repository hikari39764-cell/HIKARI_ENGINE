#pragma once
#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/HIKARI_GameObject.h"
#include <cmath>
namespace HIKARI::CAMERA {
inline MATH::Vec3 WorldPosition(const GameObject& object) noexcept {
    const auto matrix = object.GetTransform().GetWorldMatrix();
    return {matrix.m[3][0], matrix.m[3][1], matrix.m[3][2]};
}
inline bool ContainsCameraSubject(const GameObject& volume, const MATH::Vec3& halfExtents,
                                  const MATH::Vec3& worldPoint) noexcept {
    const auto inverse = MATH::Inverse(volume.GetTransform().GetWorldMatrix());
    const auto local = inverse.TransformPoint({worldPoint.x, worldPoint.y, worldPoint.z, 1});
    return MATH::IsFinite(local) && std::abs(local.x) <= halfExtents.x && std::abs(local.y) <= halfExtents.y &&
           std::abs(local.z) <= halfExtents.z;
}
} // namespace HIKARI::CAMERA
