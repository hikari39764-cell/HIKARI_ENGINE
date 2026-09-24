#pragma once

#include <array>
#include <cstddef>

#include "Render3D/Vfx/HIKARI_ActionPostReactionFrameSource.h"

namespace HIKARI::VFX {

class VfxPlaybackService;
class ActionPostReactionService;

// 画面波の枠は後処理側の上限と一致させる。独自予算を作らない。
inline constexpr size_t kMaximumVfxScreenWaves = RENDER3D::COMBAT_VFX::kMaximumPerfectGuardShockwaves;

// VFX 再生から集めた画面波。フレーム内で使い切る前提の固定長バッチで、
// 毎フレームのヒープ確保を持ち込まない。
struct VfxScreenWaveBatch {
    std::array<RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrame, kMaximumVfxScreenWaves> waves{};
    size_t count = 0u;
};

// 再生中の ScreenImpulse 出力を画面波へ変換する。年齢はエフェクト内時刻
// なので、シークやヒットストップでも波の位相が飛ばない。
size_t CollectVfxScreenWaves(const VfxPlaybackService& playback, VfxScreenWaveBatch& outBatch);

// 収集して後処理のフレーム源へ渡すまでの唯一の経路。
bool SubmitVfxScreenWaves(const VfxPlaybackService& playback, ActionPostReactionService& reactions);

} // namespace HIKARI::VFX
