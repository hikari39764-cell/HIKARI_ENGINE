#pragma once

#include <chrono>
#include "Core/HIKARI_FrameContext.h"

namespace HIKARI::TIME::INTERNAL {

// 時刻の取得とフレーム更新を分離し、長時間停止を実時間待機なしで検証する。
const FrameContext& BeginFrameAt(std::chrono::steady_clock::time_point now);

} // namespace HIKARI::TIME::INTERNAL
