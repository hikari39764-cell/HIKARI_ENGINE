#include "Core/Math/HIKARI_MathValidation.h"

#include <cmath>

namespace HIKARI::MATH {

bool IsFinite(float value) noexcept { return std::isfinite(value); }

bool IsFinite(const Vec2& value) noexcept { return IsFinite(value.x) && IsFinite(value.y); }

bool IsFinite(const Vec3& value) noexcept { return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z); }

bool IsFinite(const Vec4& value) noexcept {
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

bool IsFinite(const Quat& value) noexcept {
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

bool IsFinite(const Mat4& value) noexcept {
    for (const auto& column : value.m)
        for (float element : column)
            if (!IsFinite(element))
                return false;
    return true;
}

} // namespace HIKARI::MATH     
