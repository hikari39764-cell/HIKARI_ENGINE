#pragma once

#include <cstdint>
#include <unordered_map>

#include "Scene/Components/HIKARI_GroundAnchorComponent.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {
class World;
}

namespace HIKARI::EDITOR {

// 描画の写しに載っている物体ごとの、基点から模型の底までの高さ（模型空間、拡縮前）。模型の無い物は載らない。
std::unordered_map<uint64_t, float> CollectModelBottoms();
// 模型の底を地面へ置き、上を坂へ寄せる（前の向きは保つ）。底の中心の真下へ光線を下ろし、基点は新しい上へ底の高さだけ
// 戻すので、何度置き直しても横へずれない。bottomMeters は CollectModelBottoms の値（模型の無い物は 0 で基点を置く）。
// 真下に地面が無ければ false。
bool PlaceOnGround(const World& world, const TransformData& current, float bottomMeters,
                   const GroundPlacementSettings& settings, TransformData& out) noexcept;
// GroundAnchorComponent があればその置き方、無ければ既定。
GroundPlacementSettings ResolveGroundPlacement(const SceneObjectData& object);

} // namespace HIKARI::EDITOR
