#pragma once

#include <cstdint>
#include <vector>

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::ASSETS::COLOR {
struct ColorLutAsset;
}

// 色調整の 3D LUT。表示の符号化した色 [0, 1]^3 を入力に、暗部・中間・明部の倍率と .cube の表を一つに焼き、
// トーンマッピングの最後に一度だけ引く。
namespace HIKARI::RENDER3D::COLOR {

inline constexpr uint32_t kColorGradingLutSize = 32u;

// 倍率がすべて 1 で、LUT が無いか強さ 0 なら恒等なので焼かず引かない。
bool IsColorGradingActive(const ColorGradingSettings& settings, const ASSETS::COLOR::ColorLutAsset* lut) noexcept;
// 赤が最も速く変わる順の kColorGradingLutSize^3 個の RGBA half。gamma は表示の符号化（x^(1/gamma)）。
void BakeColorGradingLut(const ColorGradingSettings& settings, const ASSETS::COLOR::ColorLutAsset* lut, float gamma,
                         std::vector<uint16_t>& outRgbaHalf);

} // namespace HIKARI::RENDER3D::COLOR
