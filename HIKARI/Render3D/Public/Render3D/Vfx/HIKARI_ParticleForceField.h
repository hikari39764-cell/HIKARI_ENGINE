#pragma once
#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::PARTICLES {
// 座標は粒子のシミュレーション空間。半径 0 は空間全体。
struct ParticleForceField {
    MATH::Vec3 center{};
    float radius = 0;
    MATH::Vec3 axis{0, 1, 0};
    float vortex = 0;
    float turbulence = 0;
    float frequency = 1;
    float evolution = 0.3f;
    float attraction = 0;
};
// 粒子が流体に乗る割合 [1/s] の上限（GPU の 1/60 秒の分割で安定に積分できる範囲）。
inline constexpr float kMaxParticleFluidFollow = 30.0f;
// 粒子が流体へ注入する量の上限（VfxForcesModuleData::fluidSmoke など）：密度 / 秒、温度、速度の寄せ [1/s]、半径 [m]。
inline constexpr float kMaxParticleFluidSmoke = 1000.0f;
inline constexpr float kMaxParticleFluidTemperature = 64.0f;
inline constexpr float kMaxParticleFluidCoupling = 60.0f;
inline constexpr float kMinParticleFluidRadius = 0.01f;
inline constexpr float kMaxParticleFluidRadius = 4.0f;
inline bool IsParticleForceFieldActive(const ParticleForceField& field) noexcept {
    return field.turbulence != 0 || field.vortex != 0 || field.attraction != 0;
}
inline bool IsValidParticleForceField(const ParticleForceField& f) noexcept {
    const auto finite = [](const MATH::Vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    return finite(f.center) && finite(f.axis) && std::isfinite(f.radius) && f.radius >= 0 && std::isfinite(f.vortex) &&
           std::abs(f.vortex) <= 10000 && std::isfinite(f.attraction) && std::abs(f.attraction) <= 10000 &&
           std::isfinite(f.turbulence) && f.turbulence >= 0 && f.turbulence <= 10000 && std::isfinite(f.frequency) &&
           f.frequency >= 0.001f && f.frequency <= 1000 && std::isfinite(f.evolution) && std::abs(f.evolution) <= 1000;
}
} // namespace HIKARI::RENDER3D::PARTICLES
