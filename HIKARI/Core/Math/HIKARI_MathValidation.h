#pragma once

#include <cmath>

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::MATH {

bool IsFinite(float value) noexcept;
bool IsFinite(const Vec2& value) noexcept;
bool IsFinite(const Vec3& value) noexcept;
bool IsFinite(const Vec4& value) noexcept;
bool IsFinite(const Quat& value) noexcept;
bool IsFinite(const Mat4& value) noexcept;

// value が有限ならそのまま、非有限（NaN・無限）なら fallback。
inline float FiniteOr(float value, float fallback) noexcept { return std::isfinite(value) ? value : fallback; }
inline Vec3 FiniteOr(const Vec3& value, const Vec3& fallback) noexcept { return IsFinite(value) ? value : fallback; }

} // namespace HIKARI::MATH
