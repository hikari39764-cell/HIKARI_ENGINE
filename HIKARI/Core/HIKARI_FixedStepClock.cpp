#include "Core/HIKARI_FixedStepClock.h"

#include <algorithm>
#include <cmath>

namespace HIKARI {

    namespace {
        constexpr float kMinimumFixedStepSeconds = 1.0f / 1000.0f;
    }

    void FixedStepClock::SetSettings(
        const FixedStepSettings& settings) noexcept {

        settings_.stepSeconds = (std::max)(
            settings.stepSeconds,
            kMinimumFixedStepSeconds);
        settings_.maxStepsPerFrame = (std::max)(
            settings.maxStepsPerFrame,
            1u);
        settings_.maxAccumulatedSeconds = (std::max)(
            settings.maxAccumulatedSeconds,
            settings_.stepSeconds);
        accumulatorSeconds_ = (std::min)(
            accumulatorSeconds_,
            settings_.maxAccumulatedSeconds);
    }

    const FixedStepSettings& FixedStepClock::GetSettings() const noexcept {
        return settings_;
    }

    FixedStepFramePlan FixedStepClock::Advance(
        float gameDeltaSeconds) noexcept {

        FixedStepFramePlan plan{};
        plan.stepSeconds = settings_.stepSeconds;
        plan.firstTickIndex = completedTickCount_ + 1;

        const float safeDelta = (std::max)(gameDeltaSeconds, 0.0f);
        accumulatorSeconds_ += safeDelta;
        if (accumulatorSeconds_ > settings_.maxAccumulatedSeconds) {
            plan.droppedSeconds +=
                accumulatorSeconds_ - settings_.maxAccumulatedSeconds;
            accumulatorSeconds_ = settings_.maxAccumulatedSeconds;
        }

        const uint32_t availableSteps = static_cast<uint32_t>(
            std::floor(accumulatorSeconds_ / settings_.stepSeconds));
        plan.stepCount = (std::min)(
            availableSteps,
            settings_.maxStepsPerFrame);
        accumulatorSeconds_ -=
            static_cast<float>(plan.stepCount) * settings_.stepSeconds;

        if (availableSteps > plan.stepCount) {
            const uint32_t skippedSteps = availableSteps - plan.stepCount;
            const float skippedSeconds =
                static_cast<float>(skippedSteps) * settings_.stepSeconds;
            accumulatorSeconds_ = (std::max)(
                accumulatorSeconds_ - skippedSeconds,
                0.0f);
            plan.droppedSeconds += skippedSeconds;
        }

        completedTickCount_ += plan.stepCount;
        plan.interpolationAlpha = (std::clamp)(
            accumulatorSeconds_ / settings_.stepSeconds,
            0.0f,
            1.0f);
        return plan;
    }

    void FixedStepClock::Reset() noexcept {
        accumulatorSeconds_ = 0.0f;
        completedTickCount_ = 0;
    }

    uint64_t FixedStepClock::GetCompletedTickCount() const noexcept {
        return completedTickCount_;
    }

    float FixedStepClock::GetAccumulatorSeconds() const noexcept {
        return accumulatorSeconds_;
    }

} // namespace HIKARI
