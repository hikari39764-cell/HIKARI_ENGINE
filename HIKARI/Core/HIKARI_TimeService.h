#pragma once

#include "Core/HIKARI_FrameContext.h"

namespace HIKARI::TIME {

void Reset();
void ResetFrameClock();
const FrameContext& BeginFrame();
const FrameContext& GetFrameContext();

void SetGameTimeScale(float scale);
float GetGameTimeScale();

void SetPaused(bool paused);
bool IsPaused();

void SetMaxDeltaSeconds(float maxDelta);
void SetFixedDeltaSeconds(float fixedDelta);
float GetFixedDeltaSeconds();
void ReportFixedStepFrame(
    uint64_t completedTickIndex,
    uint32_t stepsThisFrame,
    float interpolationAlpha,
    float droppedSeconds);

} // namespace HIKARI::TIME
