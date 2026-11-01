#pragma once

#include <cstdint>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI::RENDERSTYLE {

// 標準 PBR の材質の主光の受け方（着色モデル）。材質 program（風格化の材質）と地形は自分の受け方を使う。
// HLSL の本体は Include/Forward/HIKARI_StyleShadingModels.hlsli。モデルを足すときは、この列挙・名前の表・
// PackRenderStyleShading・HLSL の HikariApplyStyleShading を揃えて足す。
enum class RenderStyleShadingModel : uint8_t {
    Physical,
    Cel,
};

// 保存名（render style 資産の "model"）と表示名。
inline constexpr TEXT::EnumName<RenderStyleShadingModel> kRenderStyleShadingModelNames[] = {
    {RenderStyleShadingModel::Physical, "Physical"},
    {RenderStyleShadingModel::Cel, "Cel"},
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
    // 鏡面反射の強さ（どのモデルでも掛ける）と、主光の側の縁の光。
    float specular = 1.0f;
    float rim = 0.0f;

    bool operator==(const RenderStyleShading&) const = default;
};

// 描画の光の定数（LightCB の styleShading0/1）へ詰める。0: {段数（0 で Physical）, 影の境, 境のぼかし, 鏡面反射の強さ}、
// 1: {影の側の環境光の色, 縁の光}。HLSL の HikariApplyStyleShading が同じ並びで読む。
inline void PackRenderStyleShading(const RenderStyleShading& shading, MATH::Vec4& shading0, MATH::Vec4& shading1) {
    const bool cel = shading.model == RenderStyleShadingModel::Cel;
    shading0 = {cel ? static_cast<float>(shading.bands) : 0.0f, shading.threshold, shading.softness, shading.specular};
    shading1 = {cel ? shading.shadowTint.x : 1.0f, cel ? shading.shadowTint.y : 1.0f, cel ? shading.shadowTint.z : 1.0f,
                cel ? shading.rim : 0.0f};
}

} // namespace HIKARI::RENDERSTYLE
