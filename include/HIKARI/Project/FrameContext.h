#pragma once

#include <cstdint>

namespace HIKARI {

struct FrameContext {
    // 実測フレーム間隔。長時間フレームも制限せず診断に渡す。
    float rawDt = 0.0f;
    float gameDt = 0.0f;
    // シミュレーション保護の上限適用後、時間倍率適用前の間隔。
    float unscaledDt = 0.0f;
    float fixedDt = 1.0f / 60.0f;
    float gameTimeScale = 1.0f;
    bool paused = false;
    uint64_t frameIndex = 0;
    uint64_t fixedTickIndex = 0;
    uint32_t fixedStepIndex = 0;
    uint32_t fixedStepsThisFrame = 0;
    float fixedInterpolationAlpha = 0.0f;
    float droppedFixedTime = 0.0f;
    bool isFixedStep = false;
};

} // namespace HIKARI
