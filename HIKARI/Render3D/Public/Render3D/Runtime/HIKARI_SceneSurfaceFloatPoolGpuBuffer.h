#pragma once

#include <cstddef>

#include "Render3D/Runtime/HIKARI_SceneSurfaceFloatPool.h"
#include "Render3D/Surface/HIKARI_SurfaceGpuFrameBuffer.h"

namespace HIKARI::RENDER3D::RUNTIME {

// 共有 float pool（shader の gSurfaceFeatureFloats、t1 space7）の GPU 側。float だけを上げ、sparse record は持たない
// （record 側は未使用の 1 要素）。転送量は SurfaceGpuFrameBuffer が "surfaceFeatures" の所有者で記帳する。
struct SceneSurfaceFloatPoolGpuTraits {
    static constexpr std::size_t kRecordCapacity = 1u;
    static constexpr std::size_t kHeightCapacity = kSurfaceFeatureFloatPoolCapacity;

    float MakeSentinel(std::size_t) const noexcept { return 0.0f; }
};

using SceneSurfaceFloatPoolGpuBuffer = SURFACE::SurfaceGpuFrameBuffer<float, SceneSurfaceFloatPoolGpuTraits>;

} // namespace HIKARI::RENDER3D::RUNTIME
