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

    inline constexpr uint32_t kGpuParticleFlipbookDimensionMask = 0xffu;
    inline constexpr uint32_t kGpuParticleFlipbookRowShift = 8u;
    inline constexpr uint32_t kGpuParticleFlipbookFrameMask = 0xffffu;
    inline constexpr uint32_t kGpuParticleFlipbookFlagsShift = 16u;

    constexpr uint32_t PackGpuParticleFlipbookGrid(
        uint32_t columns,
        uint32_t rows) noexcept {
        return (columns & kGpuParticleFlipbookDimensionMask) |
            ((rows & kGpuParticleFlipbookDimensionMask) <<
                kGpuParticleFlipbookRowShift);
    }

    constexpr uint32_t PackGpuParticleFlipbookPlayback(
        uint32_t frameCount,
        uint32_t flags) noexcept {
        return (frameCount & kGpuParticleFlipbookFrameMask) |
            (flags << kGpuParticleFlipbookFlagsShift);
    }

    struct GpuParticleDrawConstants {
        uint32_t aliveIndexBase = 0u;
        uint32_t billboardMode = static_cast<uint32_t>(
            ParticleBillboardMode::RotatedBillboard);
        uint32_t renderShapeMode = 0u;
        uint32_t segmentCount = 1u;
        GpuParticleFloat4 renderShapeParamsA{};
        GpuParticleFloat4 renderShapeParamsB{};
    };

    struct GpuParticleCompactConstants {
        uint32_t particleOffset = 0u;
        uint32_t particleCapacity = 0u;
        uint32_t drawIndex = 0u;
        uint32_t aliveIndexBase = 0u;
    };

    static_assert(sizeof(GpuParticle) == 96u);
    static_assert(sizeof(GpuParticleSimulationConstants) == 32u);
    static_assert(sizeof(GpuParticleDrawConstants) == 48u);
    static_assert(sizeof(GpuParticleCompactConstants) == 16u);

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
