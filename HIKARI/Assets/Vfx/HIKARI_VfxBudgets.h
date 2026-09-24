#pragma once
#include <cstdint>

namespace HIKARI::VFX {
// ソース/保存/編集の防御上限。実際の実行コストは材質コンパイラと
// 合成パス数が判定する。効果の個数を GPU 命令数と同一視しない。
inline constexpr uint32_t kMaximumVfxEffectsPerLayer = 32u;
inline constexpr uint32_t kMaximumVfxRuntimeEffectCurves = kMaximumVfxEffectsPerLayer * 9u;
} // namespace HIKARI::VFX
