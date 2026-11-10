#pragma once

#include <cstdint>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

// 光線が地面に当たった所。
struct GameProjectGroundHit {
    GameProjectFloat3 position{};
    GameProjectFloat3 normal{0.0f, 1.0f, 0.0f};
    float distance = 0.0f;
};

// モジュールの地面（地形など）を世界の地面の問い合わせへ足す接口。WorldView::AddGroundSource で登録し、外すまで生かす。
// 物理の無い編集中の世界でも当たる。所有スレッドからだけ呼ばれる。
class IGroundSource {
public:
    virtual ~IGroundSource() = default;
    // direction は正規化済み。maxDistance までに当たれば true。
    virtual bool RaycastGround(const GameProjectFloat3& origin, const GameProjectFloat3& direction, float maxDistance,
                               GameProjectGroundHit& out) const = 0;
    // 地面の形が変わるたびに変わる数。編集器はこれを見て、地面に従う物体を置き直す。
    virtual uint64_t GetGroundRevision() const noexcept = 0;
};

} // namespace HIKARI
