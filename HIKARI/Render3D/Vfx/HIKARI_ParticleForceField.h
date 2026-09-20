#pragma once
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::PARTICLES {
    // 座標は粒子のシミュレーション空間。半径 0 は空間全体。
    struct ParticleForceField {
        MATH::Vec3 center{};
        float radius = 0;
        MATH::Vec3 axis{0,1,0};
        float vortex = 0;
        float turbulence = 0;
        float frequency = 1;
        float evolution = 0.3f;
        float attraction = 0;
    };
    inline bool IsParticleForceFieldActive(const ParticleForceField& field) noexcept {
        return field.turbulence != 0 || field.vortex != 0 || field.attraction != 0;
    }
    inline bool IsValidParticleForceField(const ParticleForceField& f) noexcept {
        const auto finite = [](const MATH::Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        return finite(f.center) && finite(f.axis) && std::isfinite(f.radius) && f.radius >= 0 &&
            std::isfinite(f.vortex) && std::abs(f.vortex) <= 10000 &&
            std::isfinite(f.attraction) && std::abs(f.attraction) <= 10000 &&
            std::isfinite(f.turbulence) && f.turbulence >= 0 && f.turbulence <= 10000 &&
            std::isfinite(f.frequency) && f.frequency >= 0.001f && f.frequency <= 1000 &&
            std::isfinite(f.evolution) && std::abs(f.evolution) <= 1000;
    }
}
