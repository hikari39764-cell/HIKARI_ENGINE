#pragma once

#include <array>
#include <cstdint>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::COMBAT_VFX {

    inline constexpr uint32_t kMaximumPerfectGuardShockwaves = 4u;

    // Immutable, frame-local input published by the Scene reaction owner.
    // The renderer must consume this through a const reference and must not
    // retain it beyond the current render frame.
    struct PerfectGuardShockwaveFrame {
        MATH::Vec3 worldPosition{};
        float ageSeconds = 0.0f;
        float durationSeconds = 0.52f;
        float maximumScreenRadius = 0.46f;
        float ringThickness = 0.055f;
        float distortionStrength = 0.020f;
        float intensity = 1.0f;
        MATH::Vec3 color{ 0.32f, 0.78f, 1.0f };
        float chromaticShift = 0.0035f;
        float flashIntensity = 0.42f;
        float reserved[3] = { 0.0f, 0.0f, 0.0f };
    };

    struct PerfectGuardShockwaveFrameSource {
        uint64_t frameIndex = 0u;
        uint32_t count = 0u;
        uint32_t reserved = 0u;
        std::array<PerfectGuardShockwaveFrame,
            kMaximumPerfectGuardShockwaves> waves{};

        bool HasActiveWaves() const noexcept {
            return count != 0u;
        }

        const PerfectGuardShockwaveFrame& operator[](
            uint32_t index) const noexcept {
            return waves[index];
        }
    };

} // namespace HIKARI::RENDER3D::COMBAT_VFX
