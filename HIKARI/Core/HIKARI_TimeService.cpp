#include "Core/HIKARI_TimeService.h"

#include <algorithm>
#include <chrono>

namespace HIKARI::TIME {
namespace {

using Clock = std::chrono::steady_clock;

FrameContext gFrame{};
Clock::time_point gLastTick{};
bool gHasLastTick = false;
float gMaxDeltaSeconds = 0.1f;

} // namespace

void Reset() {
    gFrame = FrameContext{};
    gFrame.fixedDt = 1.0f / 60.0f;
    gFrame.gameTimeScale = 1.0f;
    gFrame.paused = false;
    gFrame.frameIndex = 0;

    gLastTick = Clock::time_point{};
    gHasLastTick = false;
    gMaxDeltaSeconds = 0.1f;
}

void ResetFrameClock() {
    gLastTick = Clock::time_point{};
    gHasLastTick = false;
}

const FrameContext& BeginFrame() {
    const Clock::time_point now = Clock::now();

    float rawDt = gFrame.fixedDt;
    if (gHasLastTick) {
        const std::chrono::duration<float> elapsed = now - gLastTick;
        rawDt = elapsed.count();
    }

    gLastTick = now;
    gHasLastTick = true;

    if (gMaxDeltaSeconds > 0.0f) {
        rawDt = std::min(rawDt, gMaxDeltaSeconds);
    }
    if (rawDt < 0.0f) {
        rawDt = 0.0f;
    }

    gFrame.rawDt = rawDt;
    gFrame.unscaledDt = rawDt;
    gFrame.gameDt = gFrame.paused ? 0.0f : (rawDt * gFrame.gameTimeScale);
    gFrame.fixedStepIndex = 0;
    gFrame.fixedStepsThisFrame = 0;
    gFrame.fixedInterpolationAlpha = 0.0f;
    gFrame.droppedFixedTime = 0.0f;
    gFrame.isFixedStep = false;
    ++gFrame.frameIndex;

    return gFrame;
}

const FrameContext& GetFrameContext() {
    return gFrame;
}

void SetGameTimeScale(float scale) {
    gFrame.gameTimeScale = (scale < 0.0f) ? 0.0f : scale;
}

float GetGameTimeScale() {
    return gFrame.gameTimeScale;
}

void SetPaused(bool paused) {
    gFrame.paused = paused;
}

bool IsPaused() {
    return gFrame.paused;
}

void SetMaxDeltaSeconds(float maxDelta) {
    gMaxDeltaSeconds = (maxDelta < 0.0f) ? 0.0f : maxDelta;
}

void SetFixedDeltaSeconds(float fixedDelta) {
    constexpr float kMinimumFixedDelta = 1.0f / 1000.0f;
    gFrame.fixedDt = (std::max)(fixedDelta, kMinimumFixedDelta);
}

float GetFixedDeltaSeconds() {
    return gFrame.fixedDt;
}

void ReportFixedStepFrame(
    uint64_t completedTickIndex,
    uint32_t stepsThisFrame,
    float interpolationAlpha,
    float droppedSeconds) {

    gFrame.fixedTickIndex = completedTickIndex;
    gFrame.fixedStepsThisFrame = stepsThisFrame;
    gFrame.fixedInterpolationAlpha = (std::clamp)(
        interpolationAlpha,
        0.0f,
        1.0f);
    gFrame.droppedFixedTime = (std::max)(droppedSeconds, 0.0f);
}

} // namespace HIKARI::TIME
