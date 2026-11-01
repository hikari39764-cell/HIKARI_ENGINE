#pragma once

#include <cstdint>

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::RENDERSTYLE {

// 標準 PBR の材質（と地形）の主光の受け方。材質 program（風格化の材質）は自分の受け方を使う。
enum class RenderStyleShadingModel : uint8_t {
    Physical,
    Cel,
};

struct RenderStyleShading {
    RenderStyleShadingModel model = RenderStyleShadingModel::Physical;
    // Cel：主光の当たり（余弦 × 影）を明るい側 bands 段と影の一段に分ける。threshold より暗い所が影、
    // softness は段の境のぼかし（当たりの量の幅）。
    uint32_t bands = 1u;
    float threshold = 0.3f;
    float softness = 0.03f;
    // 影の側の環境光（空・GI）に掛ける色。
    MATH::Vec3 shadowTint{0.8f, 0.76f, 0.95f};
    // 鏡面反射の強さと、主光の側の縁の光。
    float specular = 1.0f;
    float rim = 0.0f;

    bool operator==(const RenderStyleShading&) const = default;
};

} // namespace HIKARI::RENDERSTYLE
