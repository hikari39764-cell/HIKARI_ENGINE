#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include "Core/HIKARI_AssetGuid.h"
#include "RenderContracts/Material/HIKARI_MaterialEffectStack.h"

namespace HIKARI {

// 効果スタックが参照する貼図スロット。空のスロットは無効のまま扱う。
struct MaterialEffectTextureSet {
    AssetGuid baseColor{};
    AssetGuid mask{};
    AssetGuid noise{};
    // 2 つ目のノイズ場。歪み/溶解の細部を別の貼図で与える。
    AssetGuid detailNoise{};
    AssetGuid flowMap{};
};
// 全て既存の材質ノードへ展開。VFX 専用の GPU インタープリターは持たない。
MaterialAssetData BuildMaterialEffectStack(const MaterialEffectTextureSet& textures,
                                           std::span<const MaterialEffectStep> effects, bool lit = false,
                                           uint32_t atlasColumns = 1, uint32_t atlasRows = 1,
                                           std::span<const std::string> animatedParameters = {},
                                           size_t adjustmentStart = SIZE_MAX);

} // namespace HIKARI
