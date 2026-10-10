#include "Core/HIKARI_TimeService.h"
#include "Core/Internal/HIKARI_FrameClock.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace HIKARI::TIME {
namespace {

using Clock = std::chrono::steady_clock;

FrameContext gFrame{};
Clock::time_point gLastTick{};
bool gHasLastTick = false;
float gMaxDeltaSeconds = 0.1f;
float gUnscaledElapsedSeconds = 0.0f;
float gGameElapsedSeconds = 0.0f;
// 利用者が設定した一時停止。gFrame.paused はこのフレームで実際に止まっているか（锁步中は進む）。
bool gPaused = false;
uint32_t gLockstepFrames = 0u;

} // namespace

void Reset() {
    gFrame = FrameContext{};
    gFrame.fixedDt = 1.0f / 60.0f;
    gFrame.gameTimeScale = 1.0f;
    gFrame.paused = false;
    gFrame.frameIndex = 0;
    gPaused = false;
    gLockstepFrames = 0u;

    gLastTick = Clock::time_point{};
    gHasLastTick = false;
    gMaxDeltaSeconds = 0.1f;
    gUnscaledElapsedSeconds = 0.0f;
    gGameElapsedSeconds = 0.0f;
}

void ResetFrameClock() {
    gLastTick = Clock::time_point{};
    gHasLastTick = false;
}

const FrameContext& BeginFrame() { return INTERNAL::BeginFrameAt(Clock::now()); }

const FrameContext& INTERNAL::BeginFrameAt(Clock::time_point now) {
    float rawDt = gFrame.fixedDt;
    if (gHasLastTick) {
        const std::chrono::duration<float> elapsed = now - gLastTick;
        rawDt = elapsed.count();
    }

    gLastTick = now;
    gHasLastTick = true;

    if (!std::isfinite(rawDt) || rawDt < 0.0f) {
        rawDt = 0.0f;
    }

    // 計測値は保持し、シミュレーションに渡す時間だけを制限する。
    gFrame.rawDt = rawDt;
    gFrame.unscaledDt = gMaxDeltaSeconds > 0.0f ? (std::min)(rawDt, gMaxDeltaSeconds) : rawDt;
    if (gLockstepFrames > 0u) {
        --gLockstepFrames;
        gFrame.paused = false;
        gFrame.gameDt = gFrame.fixedDt;
    } else {
        gFrame.paused = gPaused;
        gFrame.gameDt = gFrame.paused ? 0.0f : (gFrame.unscaledDt * gFrame.gameTimeScale);
    }
    gUnscaledElapsedSeconds += gFrame.unscaledDt;
    gGameElapsedSeconds += gFrame.gameDt;
    gFrame.fixedStepIndex = 0;
    gFrame.fixedStepsThisFrame = 0;
    gFrame.fixedInterpolationAlpha = 0.0f;
    gFrame.droppedFixedTime = 0.0f;
    gFrame.isFixedStep = false;
    ++gFrame.frameIndex;

    return gFrame;
}

const FrameContext& GetFrameContext() { return gFrame; }

float GetUnscaledElapsedSeconds() { return gUnscaledElapsedSeconds; }

float GetInterpolatedSimulationTimeSeconds() {
    const double tick =
        static_cast<double>(gFrame.fixedTickIndex) + static_cast<double>(gFrame.fixedInterpolationAlpha);
    return static_cast<float>(tick * static_cast<double>(gFrame.fixedDt));
}

void SetGameTimeScale(float scale) { gFrame.gameTimeScale = std::isfinite(scale) ? (std::max)(scale, 0.0f) : 1.0f; }

float GetGameTimeScale() { return gFrame.gameTimeScale; }

void SetPaused(bool paused) {
    gPaused = paused;
    gFrame.paused = paused;
}

void SetLockstepFrames(uint32_t frames) { gLockstepFrames = frames; }

uint32_t GetLockstepFramesRemaining() { return gLockstepFrames; }

bool IsPaused() { return gFrame.paused; }

float GetFixedDeltaSeconds() { return gFrame.fixedDt; }

void ReportFixedStepFrame(uint64_t completedTickIndex, uint32_t stepsThisFrame, float interpolationAlpha,
                          float droppedSeconds) {
    gFrame.fixedTickIndex = completedTickIndex;
    gFrame.fixedStepsThisFrame = stepsThisFrame;
    gFrame.fixedInterpolationAlpha = (std::clamp)(interpolationAlpha, 0.0f, 1.0f);
    gFrame.droppedFixedTime = (std::max)(droppedSeconds, 0.0f);
}

} // namespace HIKARI::TIME
