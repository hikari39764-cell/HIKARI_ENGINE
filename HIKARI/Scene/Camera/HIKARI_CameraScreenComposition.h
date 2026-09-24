#pragma once

#include <limits>
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {
class CameraFollowComponent;
}
namespace HIKARI::CAMERA::FOLLOW {
struct ViewportProjection {
    float x = 0.5f;
    float y = 0.5f;
    float depth = 0.0f;
    bool valid = false;
};

struct ScreenCompositionTargets {
    float primaryX = 0.5f;
    float primaryY = 0.5f;
    float secondaryX = 0.5f;
    float secondaryY = 0.5f;
    float softHalfWidth = 0.075f;
    float softHalfHeight = 0.10f;
    float hardHalfWidth = 0.20f;
    float hardHalfHeight = 0.24f;
};

struct ScreenCompositionEvaluation {
    ViewportProjection primary{};
    ViewportProjection secondary{};
    float cost = std::numeric_limits<float>::max();
    bool horizontalSoftViolation = true;
    bool horizontalHardViolation = true;
    bool verticalSoftViolation = true;
    bool verticalHardViolation = true;

    bool HasSoftViolation() const noexcept { return horizontalSoftViolation || verticalSoftViolation; }
    bool HasHardViolation() const noexcept { return horizontalHardViolation || verticalHardViolation; }
};

ViewportProjection ProjectPointToViewport(const MATH::Vec3& eye, const MATH::Vec3& lookAt, const MATH::Vec3& point,
                                          float verticalFovRadians, float aspect) noexcept;

ScreenCompositionTargets BuildCompositionTargets(const CameraFollowComponent& component, float side,
                                                 float framingWeight, float collisionBlend) noexcept;

ScreenCompositionEvaluation EvaluateScreenComposition(const MATH::Vec3& primary, const MATH::Vec3& secondary,
                                                      const MATH::Vec3& eye, const MATH::Vec3& lookAt,
                                                      float verticalFovRadians, float aspect,
                                                      const ScreenCompositionTargets& targets) noexcept;

float SearchCompositionYaw(const CameraFollowComponent& component, const MATH::Vec3& primary,
                           const MATH::Vec3& secondary, const MATH::Vec3& lookAt, float relationYaw, float side,
                           float pitch, float distance, float verticalFovRadians, float aspect, float framingWeight,
                           float collisionBlend, const ScreenCompositionTargets& targets) noexcept;

float SearchCompositionPitch(const CameraFollowComponent& component, const MATH::Vec3& primary,
                             const MATH::Vec3& secondary, const MATH::Vec3& lookAt, float yaw, float preferredPitch,
                             float distance, float verticalFovRadians, float aspect, float framingWeight,
                             const ScreenCompositionTargets& targets) noexcept;

} // namespace HIKARI::CAMERA::FOLLOW
