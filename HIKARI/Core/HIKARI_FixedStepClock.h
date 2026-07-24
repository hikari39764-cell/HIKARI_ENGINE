#pragma once

#include <cstdint>

namespace HIKARI {

    struct FixedStepSettings {
        float stepSeconds = 1.0f / 60.0f;
        uint32_t maxStepsPerFrame = 8;
        float maxAccumulatedSeconds = 0.25f;
    };

    struct FixedStepFramePlan {
        float stepSeconds = 1.0f / 60.0f;
        float interpolationAlpha = 0.0f;
        float droppedSeconds = 0.0f;
        uint32_t stepCount = 0;
        uint64_t firstTickIndex = 0;

        uint64_t GetTickIndex(uint32_t stepIndex) const noexcept {
            return firstTickIndex + stepIndex;
        }
    };

    class FixedStepClock {
    public:
        void SetSettings(const FixedStepSettings& settings) noexcept;
        const FixedStepSettings& GetSettings() const noexcept;

        FixedStepFramePlan Advance(float gameDeltaSeconds) noexcept;
        void Reset() noexcept;

        uint64_t GetCompletedTickCount() const noexcept;
        float GetAccumulatorSeconds() const noexcept;

    private:
        FixedStepSettings settings_{};
        float accumulatorSeconds_ = 0.0f;
        uint64_t completedTickCount_ = 0;
    };

} // namespace HIKARI
