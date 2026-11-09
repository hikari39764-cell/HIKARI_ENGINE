#pragma once

#include <cstdint>

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI {
class World;

struct WorldGroundHit {
    MATH::Vec3 position{};
    MATH::Vec3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;
};

// 世界の地面：モジュールが登録した地面（IGroundSource）のうち一番近い当たり。編集中の世界でも使える。
bool RaycastWorldGround(const World& world, const MATH::Vec3& origin, const MATH::Vec3& direction, float maxDistance,
                        WorldGroundHit& outHit) noexcept;
// 登録された地面の形の版。地面が足し引きされても変わる。
uint64_t WorldGroundRevision(const World& world) noexcept;

} // namespace HIKARI
