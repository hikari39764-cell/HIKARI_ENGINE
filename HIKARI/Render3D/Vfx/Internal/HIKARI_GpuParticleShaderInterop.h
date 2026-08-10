#pragma once

#include <cstdint>

#include "Render3D/Vfx/HIKARI_GpuParticleParameterPacking.h"

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {

    struct alignas(16) GpuParticle {
        GpuParticleFloat4 positionAge{};
        GpuParticleFloat4 velocityLifetime{};
        GpuParticleFloat4 physicsPositionSeed{};
        GpuParticleFloat4 scaleRotation{};
        GpuParticleFloat4 orientation{};
        GpuParticleFloat4 color{};
    };

    struct GpuParticleSimulationConstants {
        uint32_t particleOffset = 0u;
        uint32_t particleCapacity = 0u;
        uint32_t emitterIndex = 0u;
        uint32_t spawnCursor = 0u;
        uint32_t spawnCount = 0u;
        uint32_t randomSeed = 0u;
        float deltaSeconds = 0.0f;
        uint32_t flags = 0u;
    };

    struct GpuParticleDrawConstants {
        MATH::Mat4 viewProjection = MATH::Mat4::Identity();
        GpuParticleFloat4 cameraRight{};
        GpuParticleFloat4 cameraUp{};
        GpuParticleFloat4 cameraPosition{};
        GpuParticleFloat4 timeParams{};
        GpuParticleFloat4 screenParams{};
        uint32_t billboardMode = static_cast<uint32_t>(
            ParticleBillboardMode::Fixed);
        uint32_t renderShapeMode = 0u;
        uint32_t renderShapeParam0 = 0u;
        uint32_t renderShapeParam1 = 0u;
        GpuParticleFloat4 renderShapeParamsA{};
        GpuParticleFloat4 renderShapeParamsB{};
    };

    static_assert(sizeof(GpuParticle) == 96u);
    static_assert(sizeof(GpuParticleSimulationConstants) == 32u);
    static_assert(
        sizeof(GpuParticleDrawConstants) % sizeof(uint32_t) == 0u);

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
