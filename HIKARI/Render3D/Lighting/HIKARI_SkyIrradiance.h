#pragma once

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::RENDER3D::LIGHTING {

// 軸対称の勾配空を L2 SH で Lambert 畳み込みした E/pi。
// GPU は定数 + y + y*y の RGB 多項式だけを評価する。
struct GradientSkyIrradiance {
    MATH::Vec4 coefficients[3]{};
};

GradientSkyIrradiance BuildGradientSkyIrradiance(const SkySettings& sky) noexcept;

} // namespace HIKARI::RENDER3D::LIGHTING
