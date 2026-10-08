#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

namespace HIKARI::RENDER3D::LIGHTING {

namespace {

constexpr float kMinimumConeSeparationDegrees = 0.1f;

float DegreesToRadians(float degrees) noexcept { return degrees * std::numbers::pi_v<float> / 180.0f; }

bool Finite(MATH::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

float LuminousIntensityFromFlux(float lumens) noexcept {
    return std::isfinite(lumens) ? (std::max)(lumens, 0.0f) / (4.0f * std::numbers::pi_v<float>) : 0.0f;
}

float SanitizeLocalLightSourceRadius(float radius) noexcept {
    return std::isfinite(radius) ? std::clamp(radius, 0.0f, kMaximumLocalLightSourceRadius) : 0.0f;
}

bool IsLocalLightUploadable(const SceneLightProvider& light) noexcept {
    return light.enabled && Finite(light.position) && Finite(light.color) && light.color.x >= 0 && light.color.y >= 0 &&
           light.color.z >= 0 && std::isfinite(light.intensity) && light.intensity > 0.0f &&
           std::isfinite(light.range) && light.range > 0.0f;
}

MATH::Vec3 ResolveSpotLightDirection(const SceneLightProvider& light) noexcept {
    const MATH::Vec3 direction = Finite(light.direction) ? MATH::Normalize(light.direction) : MATH::Vec3{};
    return MATH::Length(direction) > 1e-6f ? direction : MATH::Vec3{0.0f, -1.0f, 0.0f};
}

SpotLightCone ResolveSpotLightCone(const SceneLightProvider& light) noexcept {
    SpotLightCone cone{};
    cone.outerAngleDegrees =
        std::clamp(std::isfinite(light.outerConeAngleDegrees) ? light.outerConeAngleDegrees : cone.outerAngleDegrees,
                   kMinimumConeSeparationDegrees, 89.0f);
    cone.innerAngleDegrees =
        std::clamp(std::isfinite(light.innerConeAngleDegrees) ? light.innerConeAngleDegrees : cone.innerAngleDegrees,
                   0.0f, cone.outerAngleDegrees - kMinimumConeSeparationDegrees);
    cone.innerCos = std::cos(DegreesToRadians(cone.innerAngleDegrees));
    cone.outerCos = std::cos(DegreesToRadians(cone.outerAngleDegrees));
    cone.angularFalloff = std::clamp(std::isfinite(light.angularFalloff) ? light.angularFalloff : 1.0f, 0.1f, 16.0f);
    return cone;
}

} // namespace HIKARI::RENDER3D::LIGHTING
