#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::CAMERA::FOLLOW {
inline float DegreesToRadians(float degrees) noexcept { return degrees * std::numbers::pi_v<float> / 180.0f; }

inline float RadiansToDegrees(float radians) noexcept { return radians * 180.0f / std::numbers::pi_v<float>; }

inline float WrapDegrees(float degrees) noexcept {
    float wrapped = std::fmod(degrees + 180.0f, 360.0f);
    if (wrapped < 0.0f) {
        wrapped += 360.0f;
    }
    return wrapped - 180.0f;
}

inline float Lerp(float from, float to, float amount) noexcept {
    return from + (to - from) * std::clamp(amount, 0.0f, 1.0f);
}

inline MATH::Vec3 OrbitDirection(float yawDegrees, float pitchDegrees) noexcept {
    const float yaw = DegreesToRadians(yawDegrees);
    const float pitch = DegreesToRadians(pitchDegrees);
    const float horizontal = std::cos(pitch);
    return MATH::Normalize({std::sin(yaw) * horizontal, std::sin(pitch), -std::cos(yaw) * horizontal});
}

inline float SmoothFactor(float smooth, float dt) noexcept {
    if (smooth <= 0.0f || dt <= 0.0f) {
        return 1.0f;
    }
    return std::clamp(1.0f - std::exp(-smooth * dt), 0.0f, 1.0f);
}

inline float MoveTowardsAngle(float current, float target, float maxDelta) noexcept {
    const float delta = WrapDegrees(target - current);
    if (std::abs(delta) <= maxDelta) {
        return target;
    }
    return current + std::copysign(maxDelta, delta);
}

inline float SmoothDampScalar(float current, float target, float& currentVelocity, float smoothTime, float maxSpeed,
                              float dt) noexcept {
    constexpr float kSmoothEpsilon = 1.0e-5f;
    if (!std::isfinite(current) || !std::isfinite(target) || !std::isfinite(currentVelocity)) {
        currentVelocity = 0.0f;
        return std::isfinite(current) ? current : 0.0f;
    }
    if (dt <= 0.0f || !std::isfinite(dt)) {
        return current;
    }
    if (!std::isfinite(smoothTime) || smoothTime <= kSmoothEpsilon) {
        currentVelocity = 0.0f;
        return target;
    }

    smoothTime = (std::max)(smoothTime, kSmoothEpsilon);
    const float omega = 2.0f / smoothTime;
    const float x = omega * dt;
    const float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float originalTarget = target;
    float change = current - target;
    // A negative maxSpeed means unbounded smoothing.  Zero is a real
    // limit: callers use it to disable a tracking channel.
    if (std::isfinite(maxSpeed) && maxSpeed >= 0.0f) {
        const float maxChange = maxSpeed * smoothTime;
        if (std::isfinite(maxChange)) {
            change = std::clamp(change, -maxChange, maxChange);
        }
    }
    target = current - change;
    const float temp = (currentVelocity + omega * change) * dt;
    currentVelocity = (currentVelocity - omega * temp) * exp;
    float output = target + (change + temp) * exp;

    if ((originalTarget - current > 0.0f) == (output > originalTarget)) {
        output = originalTarget;
        currentVelocity = dt > kSmoothEpsilon ? (output - originalTarget) / dt : 0.0f;
    }
    if (!std::isfinite(output) || !std::isfinite(currentVelocity)) {
        currentVelocity = 0.0f;
        return originalTarget;
    }
    return output;
}

inline float SmoothDampAngle(float current, float target, float& currentVelocity, float smoothTime, float maxSpeed,
                             float dt) noexcept {
    const float delta = WrapDegrees(target - current);
    return SmoothDampScalar(current, current + delta, currentVelocity, smoothTime, maxSpeed, dt);
}

inline MATH::Vec3 SmoothDampVector(const MATH::Vec3& current, const MATH::Vec3& target, MATH::Vec3& currentVelocity,
                                   float smoothTime, float dt) noexcept {
    MATH::Vec3 result = current;
    result.x = SmoothDampScalar(current.x, target.x, currentVelocity.x, smoothTime, -1.0f, dt);
    result.y = SmoothDampScalar(current.y, target.y, currentVelocity.y, smoothTime, -1.0f, dt);
    result.z = SmoothDampScalar(current.z, target.z, currentVelocity.z, smoothTime, -1.0f, dt);
    return MATH::IsFinite(result) ? result : target;
}

} // namespace HIKARI::CAMERA::FOLLOW
