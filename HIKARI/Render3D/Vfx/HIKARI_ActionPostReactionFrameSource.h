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
        // Typed HVFX wave shaping. These are consumed by both the guard-only
        // and mask-composite post shaders; they are not renderer defaults.
        float waveFrequency = 1.0f;
        float waveAmplitude = 1.0f;
        float waveDamping = 1.0f;
        uint32_t waveCount = 1u;
    };

    // Frame-local cinematic color treatment. It is intentionally independent
    // from radial guard waves so a precision-dodge shot can hold through a
    // counter and leave on the authored recovery beat.
    struct ActionCinematicGradeFrame {
        float weight = 0.0f;
        float monochromeStrength = 0.0f;
        float chromaticSeparationPixels = 0.0f;
        float contrastStrength = 0.0f;
        uint32_t active = 0u;
        uint32_t reserved[3] = { 0u, 0u, 0u };

        bool IsActive() const noexcept {
            return active != 0u && weight > 0.0f;
        }
    };

    struct ActionBoostGradeFrame {
        float weight = 0.0f;
        float edgeIntensity = 0.0f;
        float edgeWidth = 0.20f;
        float chromaticSeparationPixels = 0.0f;
        uint32_t active = 0u;
        uint32_t reserved[3] = { 0u, 0u, 0u };

        bool IsActive() const noexcept {
            return active != 0u && weight > 0.0f &&
                edgeIntensity > 0.0f;
        }
    };

    struct PerfectGuardShockwaveFrameSource {
        uint64_t frameIndex = 0u;
        uint32_t count = 0u;
        uint32_t reserved = 0u;
        std::array<PerfectGuardShockwaveFrame,
            kMaximumPerfectGuardShockwaves> waves{};
        ActionCinematicGradeFrame cinematicGrade{};
        ActionBoostGradeFrame boostGrade{};

        bool HasActiveWaves() const noexcept {
            return count != 0u;
        }

        bool HasActiveCinematicGrade() const noexcept {
            return cinematicGrade.IsActive();
        }

        bool HasActiveBoostGrade() const noexcept {
            return boostGrade.IsActive();
        }

        const PerfectGuardShockwaveFrame& operator[](
            uint32_t index) const noexcept {
            return waves[index];
        }
    };

} // namespace HIKARI::RENDER3D::COMBAT_VFX
