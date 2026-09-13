#pragma once

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::ATMOSPHERE {

    // 描画フレームや GPU に依存しない太陽の光源・観測値。
    struct AtmosphereSolarState {
        bool valid = false;
        bool atmosphereEnabled = false;
        MATH::Vec3 sunDirection{ 0.0f, 1.0f, 0.0f };
        MATH::Vec3 sunSourceColor{ 1.0f, 1.0f, 1.0f };
        MATH::Vec3 sunColor{ 1.0f, 1.0f, 1.0f };
        MATH::Vec3 sunTransmittance{ 1.0f, 1.0f, 1.0f };
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
    // 旧 Sky の size は角度ではなく、円盤端での 1-cos(theta)。変換を共有する。
    float SolarAngularRadiusFromDiskSize(float size) noexcept;
    float SolarDiskSizeFromAngularRadius(float radius) noexcept;
    float ResolveSolarAngularRadius(const SceneEnvironment& environment) noexcept;
    AtmosphereSolarState BuildAtmosphereSolarState(const SceneEnvironment& environment) noexcept;
    AtmosphereLutConstants BuildAtmosphereLutConstants(
        const AtmosphereSettings& settings, const AtmosphereSolarState& solar,
        const MATH::Vec3& groundColor) noexcept;

} // namespace HIKARI::ATMOSPHERE
