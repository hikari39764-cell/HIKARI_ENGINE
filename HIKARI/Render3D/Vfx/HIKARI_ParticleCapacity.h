#pragma once
#include <cstdint>

namespace HIKARI::RENDER3D::PARTICLES {
// シミュレーション/圧縮 shader の thread group に合わせたプール予約量。
// 要求数と予約数を混同しないよう、コンパイラ・検証・GPU が共有する。
inline constexpr uint32_t kParticleAllocationGranularity = 64u;
constexpr uint64_t AlignParticleCapacity(uint64_t count) noexcept {
    return (count + kParticleAllocationGranularity - 1u) / kParticleAllocationGranularity *
           kParticleAllocationGranularity;
}
} // namespace HIKARI::RENDER3D::PARTICLES
