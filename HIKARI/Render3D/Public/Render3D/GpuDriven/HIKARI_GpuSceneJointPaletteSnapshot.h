#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

inline constexpr uint32_t kMaxGpuSceneJointMatricesPerSurface = 128u;
inline constexpr size_t kMaxGpuSceneJointPaletteMatrixCount = 2048u * kMaxGpuSceneJointMatricesPerSurface;

struct GpuSceneJointPaletteSnapshot {
    std::vector<MATH::Mat4> matrices{};
};

using GpuSceneJointPaletteSnapshotPtr = std::shared_ptr<const GpuSceneJointPaletteSnapshot>;

static_assert(kMaxGpuSceneJointPaletteMatrixCount <=
              (static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) / sizeof(MATH::Mat4)));

} // namespace HIKARI::RENDER3D::GPUDRIVEN
