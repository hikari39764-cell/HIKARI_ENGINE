#pragma once

#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Water/HIKARI_WaterSurfaceQueryService.h"

namespace HIKARI {
class World;
}

// 世界の水の問い合わせ：引擎の水面（WaterSurfaceQueryService）とモジュールが登録した水（IWaterSource）を合わせ、
// 問い合わせの高さに一番近い水面を返す。浮力、遊戯の公開接口、道案内が同じ答えを使う。所有スレッドからだけ呼ぶ。
namespace HIKARI::WATER {

bool SampleWorldWater(const World& world, const WaterSurfaceQuery& query, WaterSurfaceSample& outSample) noexcept;

// 相机を追う海の水面の高さと物体。無ければ false。
bool FindWorldOcean(const World& world, float& outLevel, RuntimeObjectHandle& outObject) noexcept;

} // namespace HIKARI::WATER
