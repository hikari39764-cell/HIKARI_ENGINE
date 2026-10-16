#pragma once

namespace HIKARI::MATH {

inline float Clamp01(float value) noexcept { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

// value を 0..1 に収めた三次の smoothstep。
inline float SmoothStep01(float value) noexcept {
    const float t = Clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}

// edge0..edge1 の区間で 0→1 へ滑らかに移る smoothstep。
inline float SmoothStep(float edge0, float edge1, float value) noexcept {
    return SmoothStep01((value - edge0) / (edge1 - edge0));
}

} // namespace HIKARI::MATH
