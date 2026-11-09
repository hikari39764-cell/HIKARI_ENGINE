#pragma once

#include <cstdint>
#include <unordered_map>

#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/Components/HIKARI_GroundAnchorComponent.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {
class World;
}

namespace HIKARI::EDITOR {

// 模型の外接箱（模型空間、拡縮前）。底の高さと、地面に触れる底の広さに使う。
struct ModelFootprint {
    MATH::Vec3 min{};
    MATH::Vec3 max{};
};

// 描画の写しに載っている物体ごとの模型の外接箱。模型の無い物は載らない。
std::unordered_map<uint64_t, ModelFootprint> CollectModelFootprints();
// 模型の底を地面へ置き、上を坂へ寄せる。水平の向きはそのまま保つ。
// 底の中心と四隅の真下の地面を読み、四隅で坂の向きを決め、出っ張りに角が埋まらない高さまで上げる。底より大きく
// 下がった角（崖の外）は支えに数えない。基点は新しい上へ底の高さだけ戻すので、何度置き直しても同じ所に落ち着く。
// footprint が無い物（模型の無い物）は基点を一点で置く。真下に地面が無ければ false。
bool PlaceOnGround(const World& world, const TransformData& current, const ModelFootprint* footprint,
                   const GroundPlacementSettings& settings, TransformData& out) noexcept;
// 向きは変えずに、模型の底の中心（今の姿勢）を真下の地面の高さへ合わせる。地面が無ければ false。
bool RestOnGround(const World& world, const TransformData& current, const ModelFootprint* footprint,
                  TransformData& out) noexcept;
// GroundAnchorComponent があればその置き方、無ければ既定。
GroundPlacementSettings ResolveGroundPlacement(const SceneObjectData& object);

} // namespace HIKARI::EDITOR
