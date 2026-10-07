#pragma once
#include "HIKARI/Project/ValueTypes.h"
#include "Core/Math/HIKARI_Math3D.h"
#include <cmath>
#include <cstddef>

namespace HIKARI::PROJECT::INTERNAL {
inline constexpr float kDirectionEpsilon = 1.0e-6f;

inline bool IsFinite(const GameProjectFloat3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

inline bool IsFinite(const GameProjectQuaternion& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

inline MATH::Vec3 ToRuntime(const GameProjectFloat3& value) noexcept { return {value.x, value.y, value.z}; }

inline MATH::Quat ToRuntime(const GameProjectQuaternion& value) noexcept {
    return {value.x, value.y, value.z, value.w};
}

inline GameProjectFloat3 ToProject(const MATH::Vec3& value) noexcept { return {value.x, value.y, value.z}; }

inline GameProjectQuaternion ToProject(const MATH::Quat& value) noexcept {
    return {value.x, value.y, value.z, value.w};
}

inline GameProjectFloat3 FiniteOr(const MATH::Vec3& value, const GameProjectFloat3& fallback = {}) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) ? ToProject(value) : fallback;
}

inline GameProjectQuaternion FiniteOr(const MATH::Quat& value, const GameProjectQuaternion& fallback = {}) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w)
               ? ToProject(value)
               : fallback;
}

inline bool IsFinitePositive(float value) noexcept { return std::isfinite(value) && value > 0.0f; }

inline bool IsFiniteNonNegative(float value) noexcept { return std::isfinite(value) && value >= 0.0f; }

inline bool IsFiniteNonNegative(const GameProjectFloat3& value) noexcept {
    return IsFinite(value) && value.x >= 0.0f && value.y >= 0.0f && value.z >= 0.0f;
}

inline bool HasNonZeroReserved(const uint8_t* values, size_t count) noexcept {
    if (values == nullptr) {
        return true;
    }
    for (size_t index = 0u; index < count; ++index) {
        if (values[index] != 0u) {
            return true;
        }
    }
    return false;
}

inline bool IsFiniteNonZeroDirection(const GameProjectFloat3& value) noexcept {
    if (!IsFinite(value)) {
        return false;
    }
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
    return std::isfinite(lengthSquared) && lengthSquared > kDirectionEpsilon * kDirectionEpsilon;
}

inline bool IsFiniteNonZeroRotation(const GameProjectQuaternion& value) noexcept {
    if (!IsFinite(value)) {
        return false;
    }
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
    return std::isfinite(lengthSquared) && lengthSquared > kDirectionEpsilon * kDirectionEpsilon;
}

inline bool IsValidTransform(const GameProjectTransform& transform) noexcept {
    return IsFinite(transform.position) && IsFinite(transform.rotation) && IsFinite(transform.scale) &&
           IsFiniteNonZeroRotation(transform.rotation) && transform.scale.x > 0.0f && transform.scale.y > 0.0f &&
           transform.scale.z > 0.0f;
}
} // namespace HIKARI::PROJECT::INTERNAL
