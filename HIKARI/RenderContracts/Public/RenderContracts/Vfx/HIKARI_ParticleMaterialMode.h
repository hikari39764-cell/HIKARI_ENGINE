#pragma once

#include <cstdint>

namespace HIKARI::RENDER3D::PARTICLES {

// 粒子の材質の選び方。標準材質かアセットのカスタム材質か、光照を受けるかどうかの 4 通り。
enum class ParticleMaterialMode : uint32_t {
    StandardUnlit,
    CustomMaterial,
    StandardLit,
    CustomLit,
};

constexpr bool IsCustomParticleMaterial(ParticleMaterialMode mode) noexcept {
    return mode == ParticleMaterialMode::CustomMaterial || mode == ParticleMaterialMode::CustomLit;
}
constexpr bool IsLitParticleMaterial(ParticleMaterialMode mode) noexcept {
    return mode == ParticleMaterialMode::StandardLit || mode == ParticleMaterialMode::CustomLit;
}

} // namespace HIKARI::RENDER3D::PARTICLES
