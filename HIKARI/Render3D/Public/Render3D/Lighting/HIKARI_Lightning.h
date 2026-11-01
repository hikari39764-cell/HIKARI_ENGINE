#pragma once

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI {
struct SceneEnvironment;
}

namespace HIKARI::RENDER3D::LIGHTNING {

// 落雷の光。雷雲の中の点光源として場面（局所光源）と雲（体積雲の段）を照らす。
struct LightningLight {
    bool active = false;
    // ワールド位置 [m]。視点から落雷の向きと距離、高さは雲底の 1 km 上（雷雲の中）。
    MATH::Vec3 position{};
    // 色（輝度 1）と光度 [cd]。
    MATH::Vec3 color{};
    float intensity = 0.0f;
};

// viewer は視点のワールド位置。最後の落雷の経過時間から、数回の帰還雷撃の明滅にする。
LightningLight ResolveLightningLight(const SceneEnvironment& environment, const MATH::Vec3& viewer) noexcept;

} // namespace HIKARI::RENDER3D::LIGHTNING
