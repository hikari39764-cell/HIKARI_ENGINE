#pragma once

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::VOLUMETRIC {

struct alignas(16) LocalFogVolumeGpu {
    MATH::Mat4 worldToBox{};
    MATH::Vec4 colorDensity{};
    MATH::Vec4 noiseEdgeScattering{}; // noise scale, strength, edge fraction, scatter gain
    MATH::Vec4 noiseOffset{};
};
static_assert(sizeof(LocalFogVolumeGpu) == 112u);

} // namespace HIKARI::RENDER3D::VOLUMETRIC
