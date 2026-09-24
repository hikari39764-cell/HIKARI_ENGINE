#pragma once
#include "Render3D/Vfx/HIKARI_ParticleSpatialEffect.h"
#include <algorithm>
#include <cmath>
#include "Shaders/Include/Vfx/HIKARI_ParticleSpatialMath.hlsli"
namespace HIKARI::RENDER3D::PARTICLES {
inline MATH::Vec3 RotateSpatialVector(MATH::Quat q, MATH::Vec3 v) {
    const MATH::Vec3 axis{q.x, q.y, q.z};
    const auto twice = MATH::Cross(axis, v) * 2.0f;
    return v + twice * q.w + MATH::Cross(axis, twice);
}
inline MATH::Vec3 DeformParticlePoint(MATH::Vec3 point, const ParticleSpatialEffect& effect) {
    const auto& r = effect.rotation;
    auto local = RotateSpatialVector(MATH::Quat{-r.x, -r.y, -r.z, r.w}, point - effect.origin);
    const auto safeScale = [](float value) { return std::copysign((std::max)(std::abs(value), .0001f), value); };
    const MATH::Vec3 scale{safeScale(effect.scale.x), safeScale(effect.scale.y), safeScale(effect.scale.z)};
    local = {local.x / scale.x, local.y / scale.y, local.z / scale.z};
    const auto deformed = ParticleDeformLocal(uint32_t(effect.kind), local, effect.parameters, effect.parameters2);
    const float weight = std::clamp(effect.mix, 0.0f, 1.0f) *
                         ParticleSpatialMask(uint32_t(effect.mask), effect.maskInvert, local, effect.maskParameters);
    local = local + (deformed - local) * weight;
    return effect.origin + RotateSpatialVector(r, {local.x * scale.x, local.y * scale.y, local.z * scale.z});
}
} // namespace HIKARI::RENDER3D::PARTICLES
