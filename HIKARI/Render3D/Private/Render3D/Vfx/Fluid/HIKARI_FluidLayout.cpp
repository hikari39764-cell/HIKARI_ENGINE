#include "Render3D/Vfx/HIKARI_FluidVolumePass.h"

#include <algorithm>
#include <cmath>

// 体積流体の格子と時間の決まり：画質の档位の表、域の大きさから格子、域の GPU 記憶の見積もり、固定子步の時計。
namespace HIKARI::RENDER3D::FLUID {

FluidQualityPolicy ResolveFluidQualityPolicy(FluidSmokeQuality quality) noexcept {
    switch (quality) {
    case FluidSmokeQuality::Low: return {0.75f, 1u, 48u, 2u};
    case FluidSmokeQuality::High: return {1.0f, 3u, 96u, 4u};
    case FluidSmokeQuality::Balanced:
    default: return {1.0f, 2u, 64u, 4u};
    }
}

FluidGridLayout ResolveFluidGridLayout(const MATH::Vec3& domainSize, uint32_t resolution) noexcept {
    FluidGridLayout layout{};
    const float size[3] = {(std::max)(domainSize.x, 0.01f), (std::max)(domainSize.y, 0.01f),
                           (std::max)(domainSize.z, 0.01f)};
    const float longest = (std::max)(size[0], (std::max)(size[1], size[2]));
    const uint32_t cellsOnLongest = std::clamp(resolution, kFluidResolutionMin, kFluidResolutionMax);
    layout.cellSize = longest / static_cast<float>(cellsOnLongest);
    uint32_t wanted[3]{};
    for (int axis = 0; axis < 3; ++axis)
        wanted[axis] =
            (std::max)(kFluidBrickCells, static_cast<uint32_t>(std::ceil(size[axis] / layout.cellSize - 1.0e-3f)));
    // 各辺を砖块（8）と段の倍率（2^(段 - 1)）の倍数へ切り上げ、切り上げた後の最も粗い段の各辺が 8 以下になるまで
    // 段を足す（CoarsestSolveCS は 8³ を 1 つの線程组で解く）。
    for (layout.levels = 1u;; ++layout.levels) {
        const uint32_t unit = (std::max)(kFluidBrickCells, 1u << (layout.levels - 1u));
        uint32_t coarsest = 0u;
        for (int axis = 0; axis < 3; ++axis) {
            layout.cells[axis] = (wanted[axis] + unit - 1u) / unit * unit;
            coarsest = (std::max)(coarsest, layout.cells[axis] >> (layout.levels - 1u));
        }
        if (coarsest <= 8u)
            break;
    }
    return layout;
}

uint64_t EstimateFluidDomainBytes(const FluidGridLayout& layout) noexcept {
    const uint64_t nx = layout.cells[0], ny = layout.cells[1], nz = layout.cells[2];
    uint64_t bytes = 3u * 8u * (nx + 1u) * (ny + 1u) * (nz + 1u) + 3u * 2u * nx * ny * nz +
                     4u * ((nx + 1u) / 2u) * ((ny + 1u) / 2u) * ((nz + 1u) / 2u);
    for (uint32_t level = 0u; level < layout.levels; ++level)
        bytes += 2u * 4u * (std::max<uint64_t>(nx >> level, 1u)) * (std::max<uint64_t>(ny >> level, 1u)) *
                 (std::max<uint64_t>(nz >> level, 1u));
    return bytes;
}

uint32_t FluidStepClock::Advance(double deltaSeconds, double stepSeconds, uint32_t maximumSteps) noexcept {
    if (!(stepSeconds > 0.0))
        return 0u;
    if (std::isfinite(deltaSeconds) && deltaSeconds > 0.0)
        pendingSeconds += deltaSeconds;
    // 同じゲーム時間で同じ子步の列になるよう、境目の丸めの誤差だけ許す。
    uint32_t steps = static_cast<uint32_t>(std::floor((pendingSeconds + 1.0e-7) / stepSeconds));
    pendingSeconds = (std::max)(0.0, pendingSeconds - steps * stepSeconds);
    if (steps > maximumSteps) {
        steps = maximumSteps;
        pendingSeconds = 0.0;
    }
    completedSteps += steps;
    return steps;
}

} // namespace HIKARI::RENDER3D::FLUID
