#pragma once

#include <array>
#include <cstdint>

#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {

    inline constexpr uint32_t kGpuParticleEmitterValueCount = 210u;
    inline constexpr uint32_t kGpuParticleEmitterUploadPageCount =
        kMaximumParticleEmitterSubmissionsPerFrame /
        kDefaultParticleEmitterUploadPageCapacity;

    static_assert(
        kMaximumParticleEmitterSubmissionsPerFrame %
            kDefaultParticleEmitterUploadPageCapacity == 0u);

    struct GpuParticleEmitterUploadAddress {
        uint32_t pageIndex = 0u;
        uint32_t elementIndex = 0u;
        bool valid = false;
    };

    inline GpuParticleEmitterUploadAddress
        ResolveGpuParticleEmitterUploadAddress(
            uint32_t linearIndex,
            uint32_t pageCapacity,
            uint32_t pageCount =
                kGpuParticleEmitterUploadPageCount) noexcept {

        if (pageCapacity == 0u || pageCount == 0u ||
            linearIndex / pageCapacity >= pageCount) {
            return {};
        }
        return {
            linearIndex / pageCapacity,
            linearIndex % pageCapacity,
            true
        };
    }

    struct alignas(16) GpuParticleFloat4 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;
    };

    struct alignas(16) GpuParticleEmitterParameters {
        std::array<
            GpuParticleFloat4,
            kGpuParticleEmitterValueCount> values{};
    };

    inline GpuParticleFloat4 MakeGpuParticleFloat4(
        float x,
        float y,
        float z,
        float w) noexcept {

        return { x, y, z, w };
    }

    GpuParticleEmitterParameters BuildGpuParticleEmitterParameters(
        const ParticleEmitterSubmission& submission);

    static_assert(sizeof(GpuParticleFloat4) == 16u);
    static_assert(sizeof(GpuParticleEmitterParameters) == 3360u);

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
