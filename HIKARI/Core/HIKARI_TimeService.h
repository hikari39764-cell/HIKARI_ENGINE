#pragma once

#include "HIKARI/Project/FrameContext.h"

namespace HIKARI::TIME {

void Reset();
void ResetFrameClock();
const FrameContext& BeginFrame();
const FrameContext& GetFrameContext();
float GetUnscaledElapsedSeconds();
// Fixed-step authority plus render interpolation. Rendering systems that must
// agree with deterministic simulation should consume this instead of wall time.
float GetInterpolatedSimulationTimeSeconds();

void SetGameTimeScale(float scale);
float GetGameTimeScale();

void SetPaused(bool paused);
bool IsPaused();

float GetFixedDeltaSeconds();
void ReportFixedStepFrame(uint64_t completedTickIndex, uint32_t stepsThisFrame, float interpolationAlpha,
                          float droppedSeconds);

} // namespace HIKARI::TIME
