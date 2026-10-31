#pragma once

#include <algorithm>
#include <cmath>

namespace HIKARI::MATH {

inline float Clamp01(float value) noexcept { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); }

// value が非有限なら fallback に置き換え、[minimum, maximum] に収める。
inline float ClampFinite(float value, float fallback, float minimum, float maximum) noexcept {
    return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
}

// 線形補間（amount は制限しない）。
inline float Lerp(float from, float to, float amount) noexcept { return from + (to - from) * amount; }

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
