#pragma once

#include <cstddef>
#include <vector>
#include <span>

#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

namespace HIKARI::VFX {

    class VfxPlaybackService;

    // 同時にシーンへ出せる VFX 光源の上限。ローカル光源の総数はもっと
    // 大きいが、エフェクトが光源を食い尽くさないよう出力側で絞る。
    inline constexpr size_t kMaximumVfxLightOutputs = 16u;

    // 再生中の Light 特殊出力を SceneLightProvider へ変換して追加する。
    // GPU 資源を所有せず、そのフレームの照明セットを組み立てるだけの橋渡し。
    // 上限を超える分は priority → intensity の順で落とす。
    size_t AppendVfxLightProviders(
        const VfxPlaybackService& playback,
        std::vector<RENDER3D::LIGHTING::SceneLightProvider>& outProviders,
        std::span<const uint8_t> visibleLayers = {});

} // namespace HIKARI::VFX
