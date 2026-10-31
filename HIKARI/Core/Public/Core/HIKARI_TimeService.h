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
// このフレームが止まっているか（锁步中のフレームは一時停止中でも false）。
bool IsPaused();

// 次の frames フレームは実時間と一時停止に関係なく、それぞれ固定歩長ちょうど一つ分だけゲーム時間を進める。
// 智能体の単步と入力轨道の回放を実時間の揺れから切り離して決定的にする。0 で取り消す。
void SetLockstepFrames(uint32_t frames);
uint32_t GetLockstepFramesRemaining();

float GetFixedDeltaSeconds();
void ReportFixedStepFrame(uint64_t completedTickIndex, uint32_t stepsThisFrame, float interpolationAlpha,
                          float droppedSeconds);

} // namespace HIKARI::TIME
