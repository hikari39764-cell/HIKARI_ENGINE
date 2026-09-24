#include "Render3D/Lighting/HIKARI_SkyIrradiance.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::RENDER3D::LIGHTING {

GradientSkyIrradiance BuildGradientSkyIrradiance(const SkySettings& sky) noexcept {
    const double p = std::isfinite(sky.horizonPower) ? std::max(0.01, double(sky.horizonPower)) : 1.0;
    // F(t) = smoothstep(t) * (1 - .12 * (1-t)^p)。Beta 積分で三つの帯域を厳密に投影する。
    const double beta3 = 2 / ((p + 1) * (p + 2) * (p + 3));
    const double beta4 = beta3 * 3 / (p + 4);
    const double beta5 = beta4 * 4 / (p + 5);
    const double beta6 = beta5 * 5 / (p + 6);
    const double moment0 = 0.5 - 0.12 * (3 * beta3 - 2 * beta4);
    const double moment1 = 0.35 - 0.12 * (3 * beta4 - 2 * beta5);
    const double moment2 = 4.0 / 15 - 0.12 * (3 * beta5 - 2 * beta6);
    const float quadratic = float(5.0 / 32 * (3 * moment2 - moment0));
    const auto color = [](const MATH::Vec3& value) {
        const auto channel = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 65504.0f) : 0.0f; };
        return MATH::Vec3{channel(value.x), channel(value.y), channel(value.z)};
    };
    const auto zenith = color(sky.zenithColor);
    const auto horizon = color(sky.horizonColor);
    const auto ground = color(sky.groundColor);
    const auto curvature = zenith + ground - horizon * 2;
    const MATH::Vec3 terms[]{horizon + curvature * (float(0.5 * moment0) - quadratic),
                             (zenith - ground) * float(moment1), curvature * (3 * quadratic)};
    GradientSkyIrradiance result;
    for (size_t i = 0; i < 3; ++i)
        result.coefficients[i] = {terms[i].x, terms[i].y, terms[i].z, 0};
    return result;
}

} // namespace HIKARI::RENDER3D::LIGHTING
