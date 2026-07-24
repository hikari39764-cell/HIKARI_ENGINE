#pragma once

#include <algorithm>
#include <cstdint>

namespace HIKARI::SEQUENCER {

    struct SequenceTrackId {
        uint64_t value = 0;

        bool IsValid() const noexcept {
            return value != 0;
        }
    };

    enum class SequenceInterpolationMode : uint8_t {
        Hold,
        Linear,
        Smooth,
    };

    inline float EvaluateSequenceInterpolation(
        SequenceInterpolationMode mode,
        float amount) noexcept {

        const float clamped = std::clamp(amount, 0.0f, 1.0f);
        if (mode == SequenceInterpolationMode::Hold) {
            return 0.0f;
        }
        if (mode == SequenceInterpolationMode::Smooth) {
            return clamped * clamped * (3.0f - 2.0f * clamped);
        }
        return clamped;
    }

    constexpr float kMaxSequenceDurationSeconds = 3600.0f;

} // namespace HIKARI::SEQUENCER
