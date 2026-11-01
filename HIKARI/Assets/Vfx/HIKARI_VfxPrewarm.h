#pragma once

#include <vector>

#include "RenderContracts/Asset/HIKARI_AssetTypes.h"

namespace HIKARI::VFX {

struct VfxProgram;

// 描画側が最初のフレームで先に用意する、放出器ごとの材質の参照。メッシュ出力でない粒子・形状レイヤーだけが対象。
// VfxAssetDescriptor::prewarm に入れて、描画側へ渡す。
std::vector<VfxPrewarmMaterial> CollectVfxPrewarmMaterials(const VfxProgram& program);

} // namespace HIKARI::VFX
