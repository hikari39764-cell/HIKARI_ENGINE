#pragma once
#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::EDITOR {
    struct VfxPathKnot {
        float seconds = 0;
        MATH::Vec3 position{};
        MATH::Vec3 incoming{};
        MATH::Vec3 outgoing{};
    };
    // ベジェ制御点は既存の Hermite XYZ カーブの編集表現。保存状態を増やさない。
    std::vector<VfxPathKnot> ReadVfxBezierPath(const VFX::VfxEmitterData& layer, float activeSpan);
}
