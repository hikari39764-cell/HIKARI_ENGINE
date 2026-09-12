#include "Render3D/Lighting/HIKARI_LocalLightSemantics.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

namespace HIKARI::RENDER3D::LIGHTING {

    namespace {

        constexpr float kMinimumConeSeparationDegrees = 0.1f;

        float DegreesToRadians(float degrees) noexcept {
            return degrees * std::numbers::pi_v<float> / 180.0f;
        }

        bool Finite(MATH::Vec3 value) noexcept {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        template<class Light>
        bool ValidLocalSource(const Light& light) noexcept {
            return light.enabled && Finite(light.position) && Finite(light.color) &&
                light.color.x >= 0 && light.color.y >= 0 && light.color.z >= 0 &&
                std::isfinite(light.intensity) && light.intensity > 0.0f &&
                std::isfinite(light.range) && light.range > 0.0f;
        }

    } // namespace

    bool IsLocalLightUploadable(const PointLight& light) noexcept {
        return ValidLocalSource(light);
    }

    bool IsLocalLightUploadable(const SpotLight& light) noexcept {
        return ValidLocalSource(light);
    }

    bool IsLocalLightUploadable(const SceneLightProvider& light) noexcept {
        return (light.type == SceneLightType::Point || light.type == SceneLightType::Spot) &&
            ValidLocalSource(light);
    }

    MATH::Vec3 ResolveSpotLightDirection(
        const SpotLight& light) noexcept {

        const MATH::Vec3 direction = Finite(light.direction)
            ? MATH::Normalize(light.direction) : MATH::Vec3{};
        return MATH::Length(direction) > 1e-6f
            ? direction
            : MATH::Vec3{ 0.0f, -1.0f, 0.0f };
    }

    SpotLightCone ResolveSpotLightCone(
        const SpotLight& light) noexcept {

        SpotLightCone cone{};
        cone.outerAngleDegrees = std::clamp(
            std::isfinite(light.outerConeAngleDegrees)
                ? light.outerConeAngleDegrees
                : cone.outerAngleDegrees,
            kMinimumConeSeparationDegrees,
            89.0f);
        cone.innerAngleDegrees = std::clamp(
            std::isfinite(light.innerConeAngleDegrees)
                ? light.innerConeAngleDegrees
                : cone.innerAngleDegrees,
            0.0f,
            cone.outerAngleDegrees -
                kMinimumConeSeparationDegrees);
        cone.innerCos =
            std::cos(DegreesToRadians(cone.innerAngleDegrees));
        cone.outerCos =
            std::cos(DegreesToRadians(cone.outerAngleDegrees));
        cone.angularFalloff = std::clamp(
            std::isfinite(light.angularFalloff)
                ? light.angularFalloff
                : 1.0f,
            0.1f,
            16.0f);
        return cone;
    }

    void SanitizeSpotLight(SpotLight& light) noexcept {
        const SpotLightCone cone = ResolveSpotLightCone(light);
        light.direction = ResolveSpotLightDirection(light);
        light.range = std::isfinite(light.range)
            ? (std::max)(0.0f, light.range)
            : 0.0f;
        light.intensity = std::isfinite(light.intensity)
            ? (std::max)(0.0f, light.intensity)
            : 0.0f;
        light.innerConeAngleDegrees = cone.innerAngleDegrees;
        light.outerConeAngleDegrees = cone.outerAngleDegrees;
        light.angularFalloff = cone.angularFalloff;
    }

    MATH::Vec3 ResolveSpotLightDirection(const SceneLightProvider& light) noexcept {
        SpotLight source{};
        source.direction = light.direction;
        return ResolveSpotLightDirection(source);
    }

    SpotLightCone ResolveSpotLightCone(const SceneLightProvider& light) noexcept {
        SpotLight source{};
        source.innerConeAngleDegrees = light.innerConeAngleDegrees;
        source.outerConeAngleDegrees = light.outerConeAngleDegrees;
        source.angularFalloff = light.angularFalloff;
        return ResolveSpotLightCone(source);
    }

} // namespace HIKARI::RENDER3D::LIGHTING
