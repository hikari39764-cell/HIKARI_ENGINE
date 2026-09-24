#include "Scene/Vfx/HIKARI_VfxScreenOutput.h"

#include "Scene/Vfx/HIKARI_ActionPostReactionService.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

namespace HIKARI::VFX {

size_t CollectVfxScreenWaves(const VfxPlaybackService& playback, VfxScreenWaveBatch& outBatch) {
    return playback.CollectScreenWaves(outBatch);
}

bool SubmitVfxScreenWaves(const VfxPlaybackService& playback, ActionPostReactionService& reactions) {
    VfxScreenWaveBatch batch{};
    const size_t count = CollectVfxScreenWaves(playback, batch);
    // 波が無いフレームも必ず送る。前フレームの波が残り続けない。
    reactions.SubmitVfxScreenWaves(
        std::span<const RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrame>(batch.waves.data(), count));
    return count > 0u;
}

} // namespace HIKARI::VFX
