#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::ATMOSPHERE {
float SanitizeFiniteRange(float value, float fallback, float minimum, float maximum) noexcept {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, minimum, maximum);
}

MATH::Vec3 SanitizeDirection(const MATH::Vec3& value, const MATH::Vec3& fallback) noexcept {
    const auto NormalizeFinite = [](const MATH::Vec3& candidate) {
        if (!std::isfinite(candidate.x) || !std::isfinite(candidate.y) || !std::isfinite(candidate.z)) {
            return MATH::Vec3{0.0f, -1.0f, 0.0f};
        }
        const float length = MATH::Length(candidate);
        if (!std::isfinite(length) || length < 1e-5f) {
            return MATH::Vec3{0.0f, -1.0f, 0.0f};
        }
        return candidate * (1.0f / length);
    };
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) || MATH::Length(value) < 1e-5f) {
        return NormalizeFinite(fallback);
    }
    return NormalizeFinite(value);
}

namespace {

// 光源の色味（強さは照度が持つ）。
MATH::Vec3 SanitizeLightColor(const MATH::Vec3& value) noexcept {
    return {
        SanitizeFiniteRange(value.x, 1.0f, 0.0f, 4.0f),
        SanitizeFiniteRange(value.y, 1.0f, 0.0f, 4.0f),
        SanitizeFiniteRange(value.z, 1.0f, 0.0f, 4.0f),
    };
}

MATH::Vec3 EstimateAtmosphereSunTransmittance(const AtmosphereLutConstants& atmosphere, const MATH::Vec3& sunDirection,
                                              float observerHeight) noexcept {
    // Match the GPU atmosphere's spherical ground-to-space optical
    // path closely enough for the disk, glare, fog, and water to
    // share one observed solar term.  This is deliberately a small
    // fixed integral: the full view-path scattering remains GPU-owned.
    if (!std::isfinite(sunDirection.y))
        return {};
    constexpr uint32_t kSampleCount = 24u;
    const float planetRadius = atmosphere.planetParams.x;
    const float topRadius = atmosphere.planetParams.y;
    const float rayleighHeight = atmosphere.planetParams.z;
    const float mieHeight = atmosphere.planetParams.w;
    const float ozoneWidth = atmosphere.ozoneParams.x;
    const float ozoneCenter = atmosphere.ozoneAbsorption.w;
    const float observerRadius =
        planetRadius + SanitizeFiniteRange(observerHeight, 2.0f, 1.0f, std::max(1.0f, topRadius - planetRadius - 1.0f));
    const float mu = SanitizeFiniteRange(sunDirection.y, 1.0f, -1.0f, 1.0f);
    const float topDiscriminant = observerRadius * observerRadius * (mu * mu - 1.0f) + topRadius * topRadius;
    if (topDiscriminant <= 0.0f)
        return {};
    const float topDistance = -observerRadius * mu + std::sqrt(topDiscriminant);
    if (!std::isfinite(topDistance) || topDistance <= 0.0f)
        return {};
    const float groundDiscriminant = observerRadius * observerRadius * (mu * mu - 1.0f) + planetRadius * planetRadius;
    if (groundDiscriminant >= 0.0f) {
        const float groundDistance = -observerRadius * mu - std::sqrt(groundDiscriminant);
        if (groundDistance > 0.0f && groundDistance < topDistance) {
            return {};
        }
    }

    const float stepLength = topDistance / static_cast<float>(kSampleCount);
    float rayleighColumn = 0.0f;
    float mieColumn = 0.0f;
    float ozoneColumn = 0.0f;
    for (uint32_t sample = 0u; sample < kSampleCount; ++sample) {
        const float distance = (static_cast<float>(sample) + 0.5f) * stepLength;
        const float radiusSquared =
            observerRadius * observerRadius + distance * distance + 2.0f * observerRadius * mu * distance;
        const float height = std::max(0.0f, std::sqrt(std::max(radiusSquared, 0.0f)) - planetRadius);
        rayleighColumn += std::exp(-height / rayleighHeight) * stepLength;
        mieColumn += std::exp(-height / mieHeight) * stepLength;
        ozoneColumn += (std::max)(0.0f, 1.0f - std::abs(height - ozoneCenter) / ozoneWidth) * stepLength;
    }
    const MATH::Vec3 opticalDepth{
        atmosphere.rayleighScattering.x * rayleighColumn + atmosphere.mieScattering.x * mieColumn +
            atmosphere.ozoneAbsorption.x * ozoneColumn,
        atmosphere.rayleighScattering.y * rayleighColumn + atmosphere.mieScattering.y * mieColumn +
            atmosphere.ozoneAbsorption.y * ozoneColumn,
        atmosphere.rayleighScattering.z * rayleighColumn + atmosphere.mieScattering.z * mieColumn +
            atmosphere.ozoneAbsorption.z * ozoneColumn,
    };
    return {
        std::exp(-std::clamp(opticalDepth.x, 0.0f, 32.0f)),
        std::exp(-std::clamp(opticalDepth.y, 0.0f, 32.0f)),
        std::exp(-std::clamp(opticalDepth.z, 0.0f, 32.0f)),
    };
}

} // namespace

AtmosphereSolarState BuildAtmosphereSolarState(const SceneEnvironment& environment, float observerHeight) noexcept {
    AtmosphereSolarState result{};
    const EnvironmentDirectionalLight& light = environment.frame.Primary();
    result.sunDirection = SanitizeDirection(light.toLight, {0.0f, 1.0f, 0.0f});
    result.sunSourceColor = SanitizeLightColor(light.color);
    result.sunColor = result.sunSourceColor;
    result.atmosphereEnabled = environment.sky.source == SkySource::Atmosphere;
    // 大気の空は地平線の下の光源も残し、地球の影（透過率）で地表の直射を消す。大気を持たない空は円盤の見える割合で消す。
    const float visibility = result.atmosphereEnabled ? 1.0f : std::clamp(light.horizonVisibility, 0.0f, 1.0f);
    result.sunSourceRadiance =
        std::isfinite(light.illuminance) ? (std::max)(light.illuminance, 0.0f) * visibility : 0.0f;
    result.sunAngularRadius =
        SanitizeFiniteRange(light.angularRadius, 0.004675f, kMinSolarAngularRadius, kMaxSolarAngularRadius);
    result.sunTransmittance =
        result.atmosphereEnabled
            ? EstimateAtmosphereSunTransmittance(BuildAtmosphereLutConstants(environment.atmosphere, result),
                                                 result.sunDirection, observerHeight)
            : MATH::Vec3{1.0f, 1.0f, 1.0f};
    const float transmittanceLuminance = SanitizeFiniteRange(
        result.sunTransmittance.x * 0.2126f + result.sunTransmittance.y * 0.7152f + result.sunTransmittance.z * 0.0722f,
        0.0f, 0.0f, 1.0f);
    result.sunRadiance = result.sunSourceRadiance * transmittanceLuminance;
    if (transmittanceLuminance > 1.0e-5f) {
        // 正規化した色度を光源色の上限で再び clamp すると HDR の積が変わる。
        result.sunColor = {
            result.sunSourceColor.x * result.sunTransmittance.x / transmittanceLuminance,
            result.sunSourceColor.y * result.sunTransmittance.y / transmittanceLuminance,
            result.sunSourceColor.z * result.sunTransmittance.z / transmittanceLuminance,
        };
    }
    result.valid = result.sunSourceRadiance > 0.0f;
    return result;
}

AtmosphereLutConstants BuildAtmosphereLutConstants(const AtmosphereSettings& atmosphere,
                                                   const AtmosphereSolarState& solar) noexcept {
    AtmosphereLutConstants result{};
    if (!solar.atmosphereEnabled)
        return result;
    // 有限でない値は既定の大気へ戻す。
    const AtmosphereSettings defaults{};
    // 地面の反照率は色で持ち、shader が掛ける ozoneParams.w の単一倍率は 1 にする。
    result.groundColor = {SanitizeFiniteRange(atmosphere.groundAlbedo.x, defaults.groundAlbedo.x, 0.0f, 1.0f),
                          SanitizeFiniteRange(atmosphere.groundAlbedo.y, defaults.groundAlbedo.y, 0.0f, 1.0f),
                          SanitizeFiniteRange(atmosphere.groundAlbedo.z, defaults.groundAlbedo.z, 0.0f, 1.0f), 0.0f};
    const float planetRadius = SanitizeFiniteRange(atmosphere.planetRadius, defaults.planetRadius, 1.0f, 100000000.0f);
    const float height = SanitizeFiniteRange(atmosphere.atmosphereHeight, defaults.atmosphereHeight, 1.0f, 1000000.0f);
    result.planetParams = {
        planetRadius,
        planetRadius + std::max(height, planetRadius * 2.0e-7f),
        SanitizeFiniteRange(atmosphere.rayleighScaleHeight, defaults.rayleighScaleHeight, 1.0f, 1000000.0f),
        SanitizeFiniteRange(atmosphere.mieScaleHeight, defaults.mieScaleHeight, 1.0f, 1000000.0f),
    };
    result.rayleighScattering = {
        SanitizeFiniteRange(atmosphere.rayleighScattering.x, defaults.rayleighScattering.x, 0.0f, 1.0f),
        SanitizeFiniteRange(atmosphere.rayleighScattering.y, defaults.rayleighScattering.y, 0.0f, 1.0f),
        SanitizeFiniteRange(atmosphere.rayleighScattering.z, defaults.rayleighScattering.z, 0.0f, 1.0f),
        SanitizeFiniteRange(solar.sunAngularRadius, 0.004675f, kMinSolarAngularRadius, kMaxSolarAngularRadius),
    };
    result.mieScattering = {
        SanitizeFiniteRange(atmosphere.mieScattering.x, defaults.mieScattering.x, 0.0f, 1.0f),
        SanitizeFiniteRange(atmosphere.mieScattering.y, defaults.mieScattering.y, 0.0f, 1.0f),
        SanitizeFiniteRange(atmosphere.mieScattering.z, defaults.mieScattering.z, 0.0f, 1.0f),
        SanitizeFiniteRange(atmosphere.mieAnisotropy, defaults.mieAnisotropy, -0.95f, 0.95f),
    };
    result.ozoneAbsorption = {
        SanitizeFiniteRange(atmosphere.ozoneAbsorption.x, defaults.ozoneAbsorption.x, 0.0f, 1.0f),
        SanitizeFiniteRange(atmosphere.ozoneAbsorption.y, defaults.ozoneAbsorption.y, 0.0f, 1.0f),
        SanitizeFiniteRange(atmosphere.ozoneAbsorption.z, defaults.ozoneAbsorption.z, 0.0f, 1.0f),
        SanitizeFiniteRange(atmosphere.ozoneCenterHeight, defaults.ozoneCenterHeight, 0.0f, 1000000.0f),
    };
    result.ozoneParams = {
        SanitizeFiniteRange(atmosphere.ozoneWidth, defaults.ozoneWidth, 1.0f, 1000000.0f),
        SanitizeFiniteRange(atmosphere.aerialPerspectiveStrength, defaults.aerialPerspectiveStrength, 0.0f, 8.0f),
        SanitizeFiniteRange(solar.sunTransmittance.x * 0.2126f + solar.sunTransmittance.y * 0.7152f +
                                solar.sunTransmittance.z * 0.0722f,
                            0.0f, 0.0f, 1.0f),
        1.0f,
    };
    result.sunDirection = {
        solar.sunDirection.x,
        solar.sunDirection.y,
        solar.sunDirection.z,
        1.0f,
    };
    result.sunColorRadiance = {
        solar.sunSourceColor.x,
        solar.sunSourceColor.y,
        solar.sunSourceColor.z,
        solar.sunSourceRadiance,
    };

    return result;
}

} // namespace HIKARI::ATMOSPHERE
