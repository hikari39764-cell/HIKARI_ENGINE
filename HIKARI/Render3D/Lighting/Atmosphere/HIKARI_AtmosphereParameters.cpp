#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::ATMOSPHERE {
    namespace {

        float SanitizeFiniteRange(
            float value,
            float fallback,
            float minimum,
            float maximum) noexcept {
            if (!std::isfinite(value)) {
                return fallback;
            }
            return std::clamp(value, minimum, maximum);
        }

        MATH::Vec3 SanitizeDirection(const MATH::Vec3& value, const MATH::Vec3& fallback) noexcept {
            const auto NormalizeFinite = [](const MATH::Vec3& candidate) {
                if (!std::isfinite(candidate.x) ||
                    !std::isfinite(candidate.y) ||
                    !std::isfinite(candidate.z)) {
                    return MATH::Vec3{ 0.0f, -1.0f, 0.0f };
                }
                const float length = MATH::Length(candidate);
                if (!std::isfinite(length) || length < 1e-5f) {
                    return MATH::Vec3{ 0.0f, -1.0f, 0.0f };
                }
                return candidate * (1.0f / length);
            };
            if (!std::isfinite(value.x) ||
                !std::isfinite(value.y) ||
                !std::isfinite(value.z) ||
                MATH::Length(value) < 1e-5f) {
                return NormalizeFinite(fallback);
            }
            return NormalizeFinite(value);
        }

        MATH::Vec3 SanitizeRadianceColor(
            const MATH::Vec3& value,
            const MATH::Vec3& fallback = { 1.0f, 1.0f, 1.0f }) noexcept {
            return {
                SanitizeFiniteRange(value.x, fallback.x, 0.0f, 64.0f),
                SanitizeFiniteRange(value.y, fallback.y, 0.0f, 64.0f),
                SanitizeFiniteRange(value.z, fallback.z, 0.0f, 64.0f),
            };
        }

        MATH::Vec3 EstimateAtmosphereSunTransmittance(
            const AtmosphereLutConstants& atmosphere,
            const MATH::Vec3& sunDirection) noexcept {
            // Match the GPU atmosphere's spherical ground-to-space optical
            // path closely enough for the disk, glare, fog, and water to
            // share one observed solar term.  This is deliberately a small
            // fixed integral: the full view-path scattering remains GPU-owned.
            if (!std::isfinite(sunDirection.y)) return {};
            constexpr uint32_t kSampleCount = 24u;
            const float planetRadius = atmosphere.planetParams.x;
            const float topRadius = atmosphere.planetParams.y;
            const float rayleighHeight = atmosphere.planetParams.z;
            const float mieHeight = atmosphere.planetParams.w;
            const float ozoneWidth = atmosphere.ozoneParams.x;
            const float ozoneCenter = atmosphere.ozoneAbsorption.w;
            const float observerRadius = planetRadius + 2.0f;
            const float mu = SanitizeFiniteRange(
                sunDirection.y, 1.0f, -1.0f, 1.0f);
            const float topDiscriminant = observerRadius * observerRadius *
                (mu * mu - 1.0f) + topRadius * topRadius;
            if (topDiscriminant <= 0.0f) return {};
            const float topDistance = -observerRadius * mu +
                std::sqrt(topDiscriminant);
            if (!std::isfinite(topDistance) || topDistance <= 0.0f) return {};
            const float groundDiscriminant = observerRadius * observerRadius *
                (mu * mu - 1.0f) + planetRadius * planetRadius;
            if (groundDiscriminant >= 0.0f) {
                const float groundDistance = -observerRadius * mu -
                    std::sqrt(groundDiscriminant);
                if (groundDistance > 0.0f && groundDistance < topDistance) {
                    return {};
                }
            }

            const float stepLength = topDistance /
                static_cast<float>(kSampleCount);
            float rayleighColumn = 0.0f;
            float mieColumn = 0.0f;
            float ozoneColumn = 0.0f;
            for (uint32_t sample = 0u; sample < kSampleCount; ++sample) {
                const float distance =
                    (static_cast<float>(sample) + 0.5f) * stepLength;
                const float radiusSquared = observerRadius * observerRadius +
                    distance * distance +
                    2.0f * observerRadius * mu * distance;
                const float height = std::max(
                    0.0f,
                    std::sqrt(std::max(radiusSquared, 0.0f)) - planetRadius);
                rayleighColumn += std::exp(-height / rayleighHeight) * stepLength;
                mieColumn += std::exp(-height / mieHeight) * stepLength;
                ozoneColumn += std::exp(
                    -std::abs(height - ozoneCenter) / ozoneWidth) * stepLength;
            }
            const MATH::Vec3 opticalDepth{
                atmosphere.rayleighScattering.x * rayleighColumn + atmosphere.mieScattering.x * mieColumn + atmosphere.ozoneAbsorption.x * ozoneColumn,
                atmosphere.rayleighScattering.y * rayleighColumn + atmosphere.mieScattering.y * mieColumn + atmosphere.ozoneAbsorption.y * ozoneColumn,
                atmosphere.rayleighScattering.z * rayleighColumn + atmosphere.mieScattering.z * mieColumn + atmosphere.ozoneAbsorption.z * ozoneColumn,
            };
            return {
                std::exp(-std::clamp(opticalDepth.x, 0.0f, 32.0f)),
                std::exp(-std::clamp(opticalDepth.y, 0.0f, 32.0f)),
                std::exp(-std::clamp(opticalDepth.z, 0.0f, 32.0f)),
            };
        }

    } // namespace

    AtmosphereSolarState BuildAtmosphereSolarState(const SceneEnvironment& environment) noexcept {
        AtmosphereSolarState result{};
        const DirectionalLight& sun = environment.directional;
        const MATH::Vec3 directional = SanitizeDirection(
            sun.direction,
            { 0.4f, -1.0f, -0.6f });
        result.sunDirection = directional * -1.0f;
        result.sunSourceColor = SanitizeRadianceColor(sun.color);
        result.sunColor = result.sunSourceColor;
        const float directionalIntensity = sun.enabled
            ? SanitizeFiniteRange(sun.intensity, 0.0f, 0.0f, 64.0f)
            : 0.0f;
        result.atmosphereEnabled =
            environment.sky.enabled &&
            environment.sky.mode == SkyMode::Atmosphere &&
            environment.atmosphere.enabled;
        const float solarScale = result.atmosphereEnabled
            ? SanitizeFiniteRange(
                environment.atmosphere.sunIlluminance,
                1.0f,
                0.0f,
                16.0f) *
                std::exp2(SanitizeFiniteRange(
                    environment.atmosphere.sunExposure,
                    0.0f,
                    -16.0f,
                    16.0f))
            : 1.0f;
        result.sunSourceRadiance = SanitizeFiniteRange(
            directionalIntensity * solarScale,
            0.0f,
            0.0f,
            256.0f);
        if (result.atmosphereEnabled) {
            result.sunTransmittance = EstimateAtmosphereSunTransmittance(
                BuildAtmosphereLutConstants(environment.atmosphere, result, environment.sky.groundColor),
                result.sunDirection);
        } else {
            result.sunTransmittance = { 1.0f, 1.0f, 1.0f };
        }
        const float transmittanceLuminance = SanitizeFiniteRange(
            result.sunTransmittance.x * 0.2126f +
                result.sunTransmittance.y * 0.7152f +
                result.sunTransmittance.z * 0.0722f,
            0.0f,
            0.0f,
            1.0f);
        result.sunRadiance = result.sunSourceRadiance *
            transmittanceLuminance;
        if (transmittanceLuminance > 1.0e-5f) {
            result.sunColor = SanitizeRadianceColor({
                result.sunSourceColor.x * result.sunTransmittance.x /
                    transmittanceLuminance,
                result.sunSourceColor.y * result.sunTransmittance.y /
                    transmittanceLuminance,
                result.sunSourceColor.z * result.sunTransmittance.z /
                    transmittanceLuminance,
            });
        }
        result.sunAngularRadius = result.atmosphereEnabled
            ? SanitizeFiniteRange(
                environment.atmosphere.sunAngularRadius,
                0.004675f,
                0.0001f,
                0.25f)
            : SanitizeFiniteRange(
                environment.sky.sunDiskSize * 0.003f,
                0.004675f,
                0.0001f,
                0.25f);
        result.valid = sun.enabled;
        return result;
    }

    AtmosphereLutConstants BuildAtmosphereLutConstants(
        const AtmosphereSettings& atmosphere, const AtmosphereSolarState& solar,
        const MATH::Vec3& groundColor) noexcept {
        AtmosphereLutConstants result{};
        if (!solar.atmosphereEnabled) return result;
        result.groundColor = {
            SanitizeFiniteRange(groundColor.x, 0.1f, 0.0f, 1.0f),
            SanitizeFiniteRange(groundColor.y, 0.14f, 0.0f, 1.0f),
            SanitizeFiniteRange(groundColor.z, 0.2f, 0.0f, 1.0f), 0.0f};
        const float planetRadius = SanitizeFiniteRange(atmosphere.planetRadius, 6360000.0f, 1.0f, 100000000.0f);
        const float height = SanitizeFiniteRange(atmosphere.atmosphereHeight, 80000.0f, 1.0f, 1000000.0f);
        result.planetParams = {
            planetRadius, planetRadius + std::max(height, planetRadius * 2.0e-7f),
            SanitizeFiniteRange(atmosphere.rayleighScaleHeight, 8000.0f, 1.0f, 1000000.0f),
            SanitizeFiniteRange(atmosphere.mieScaleHeight, 1200.0f, 1.0f, 1000000.0f),
        };
        result.rayleighScattering = {
            SanitizeFiniteRange(atmosphere.rayleighScattering.x, 5.8e-6f, 0.0f, 1.0f),
            SanitizeFiniteRange(atmosphere.rayleighScattering.y, 13.5e-6f, 0.0f, 1.0f),
            SanitizeFiniteRange(atmosphere.rayleighScattering.z, 33.1e-6f, 0.0f, 1.0f),
            SanitizeFiniteRange(
                atmosphere.sunAngularRadius,
                0.004675f,
                0.0001f,
                0.25f),
        };
        result.mieScattering = {
            SanitizeFiniteRange(atmosphere.mieScattering.x, 3.996e-6f, 0.0f, 1.0f),
            SanitizeFiniteRange(atmosphere.mieScattering.y, 3.996e-6f, 0.0f, 1.0f),
            SanitizeFiniteRange(atmosphere.mieScattering.z, 3.996e-6f, 0.0f, 1.0f),
            SanitizeFiniteRange(atmosphere.mieAnisotropy, 0.76f, -0.95f, 0.95f),
        };
        result.ozoneAbsorption = {
            SanitizeFiniteRange(atmosphere.ozoneAbsorption.x, 6.5e-7f, 0.0f, 1.0f),
            SanitizeFiniteRange(atmosphere.ozoneAbsorption.y, 1.881e-6f, 0.0f, 1.0f),
            SanitizeFiniteRange(atmosphere.ozoneAbsorption.z, 8.5e-8f, 0.0f, 1.0f),
            SanitizeFiniteRange(atmosphere.ozoneCenterHeight, 25000.0f, 0.0f, 1000000.0f),
        };
        result.ozoneParams = {
            SanitizeFiniteRange(atmosphere.ozoneWidth, 15000.0f, 1.0f, 1000000.0f),
            SanitizeFiniteRange(
                atmosphere.aerialPerspectiveStrength,
                1.0f,
                0.0f,
                8.0f),
            SanitizeFiniteRange(
                solar.sunTransmittance.x * 0.2126f +
                    solar.sunTransmittance.y * 0.7152f +
                    solar.sunTransmittance.z * 0.0722f,
                0.0f,
                0.0f,
                1.0f),
            SanitizeFiniteRange(atmosphere.groundAlbedo, 0.1f, 0.0f, 1.0f),
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
