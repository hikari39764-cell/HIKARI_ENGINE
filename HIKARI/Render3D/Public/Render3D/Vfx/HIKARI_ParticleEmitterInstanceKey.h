#pragma once

#include <cstdint>

namespace HIKARI::RENDER3D::PARTICLES {

// 再生実例の 1 層を GPU 側で指す键（粒子・形状・流体の層が共用する）。
struct ParticleEmitterInstanceKey {
    uint64_t playbackNamespace = 0u;
    uint32_t playbackSlot = 0u;
    uint32_t playbackGeneration = 0u;
    uint32_t emitterIndex = 0u;

    bool operator==(const ParticleEmitterInstanceKey&) const noexcept = default;
};

} // namespace HIKARI::RENDER3D::PARTICLES
