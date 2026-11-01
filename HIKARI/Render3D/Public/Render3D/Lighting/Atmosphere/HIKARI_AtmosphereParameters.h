#pragma once

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::ATMOSPHERE {

float SanitizeFiniteRange(float value, float fallback, float minimum, float maximum) noexcept;
MATH::Vec3 SanitizeDirection(const MATH::Vec3& value, const MATH::Vec3& fallback) noexcept;

// 描画フレームや GPU に依存しない主光源（太陽、夜は月）の光源・観測値。
struct AtmosphereSolarState {
    bool valid = false;
    bool atmosphereEnabled = false;
    // 光源へ向かう単位方向。
    MATH::Vec3 sunDirection{0.0f, 1.0f, 0.0f};
    MATH::Vec3 sunSourceColor{1.0f, 1.0f, 1.0f};
    MATH::Vec3 sunColor{1.0f, 1.0f, 1.0f};
    MATH::Vec3 sunTransmittance{1.0f, 1.0f, 1.0f};
    // 大気外と地表の法線照度 [lux]。color * 照度が、面法線が光源を向くときの入射照度。
    // BRDF/phase/円盤が各々の積分を持つ。
    float sunSourceRadiance = 0.0f;
    float sunRadiance = 0.0f;
    float sunAngularRadius = 0.004675f;
};

struct AtmosphereLutConstants {
    MATH::Vec4 planetParams{0, 0, 0, 0};
    MATH::Vec4 rayleighScattering{0, 0, 0, 0};
    MATH::Vec4 mieScattering{0, 0, 0, 0};
    MATH::Vec4 ozoneAbsorption{0, 0, 0, 0};
    // ozone width, aerial strength, ground solar transmittance luminance, albedo.
    MATH::Vec4 ozoneParams{0, 0, 0, 0};
    MATH::Vec4 sunDirection{0, 0, 0, 0};
    // 光源側の放射輝度。地表までの透過率を二重適用しない。
    MATH::Vec4 sunColorRadiance{0, 0, 0, 0};
    MATH::Vec4 groundColor{0, 0, 0, 0};
};
static_assert(sizeof(AtmosphereLutConstants) == 128u);

inline constexpr float kMinSolarAngularRadius = 0.0001f;
inline constexpr float kMaxSolarAngularRadius = 0.25f;
AtmosphereSolarState BuildAtmosphereSolarState(const SceneEnvironment& environment,
                                               float observerHeight = 2.0f) noexcept;
AtmosphereLutConstants BuildAtmosphereLutConstants(const AtmosphereSettings& settings,
                                                   const AtmosphereSolarState& solar) noexcept;

} // namespace HIKARI::ATMOSPHERE
