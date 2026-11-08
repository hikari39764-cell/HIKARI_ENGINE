#include "Render3D/Vfx/HIKARI_FluidVolumePass.h"

#include <algorithm>
#include <cmath>

// 体積流体の格子と時間の決まり：画質の档位の表、域の大きさから格子、域の GPU 記憶の見積もり、固定子步の時計、
// 子步の長さと解算の LOD（視図から域ごとに子步を長くするか決める）。
namespace HIKARI::RENDER3D::FLUID {

FluidQualityPolicy ResolveFluidQualityPolicy(FluidSmokeQuality quality) noexcept {
    switch (quality) {
    case FluidSmokeQuality::Low: return {0.75f, 1u, 48u, 2u, 2.0f};
    case FluidSmokeQuality::High: return {1.0f, 3u, 96u, 4u, 0.5f};
    case FluidSmokeQuality::Balanced:
    default: return {1.0f, 2u, 64u, 4u, 1.0f};
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
    const uint64_t densityBytes = layout.cellColor ? 8u : 2u;
    const uint64_t fuelBytes = layout.combustion ? 2u * 4u : 0u;
    uint64_t bytes = 3u * 8u * (nx + 1u) * (ny + 1u) * (nz + 1u) + (3u * densityBytes + fuelBytes) * nx * ny * nz +
                     8u * ((nx + 1u) / 2u) * ((ny + 1u) / 2u) * ((nz + 1u) / 2u);
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

namespace {
// 解算の LOD の子步の上限（それより長い子步は平流がぼやけ、源の掃きが粗くなる）。作者の子步の方が長ければそれ。
constexpr double kLodLongestSubstepSeconds = 0.1;
} // namespace

double FluidSubstepSeconds(const FluidDomainSettings& settings, uint32_t lod) noexcept {
    const double authored = 1.0 / static_cast<double>(std::clamp(settings.substepRate, 15.0f, 240.0f));
    if (lod == 0u)
        return authored;
    return (std::min)(authored * static_cast<double>(1u << lod), (std::max)(authored, kLodLongestSubstepSeconds));
}

uint32_t ResolveFluidSimulationLod(const FluidDomainSubmission& submission, float cellSize,
                                   const FluidFrameSettings& settings, const FluidQualityPolicy& policy,
                                   uint32_t previous) noexcept {
    if (settings.viewHeight <= 0.0f || policy.lodCellPixels <= 0.0f)
        return 0u;
    const MATH::Mat4& m = settings.relativeViewProj;
    const MATH::Vec3 center = submission.worldPosition - settings.viewerPosition;
    const float radius = 0.5f * MATH::Length(submission.settings.domainSize);
    // 錐台の左右上下の面（行 3 ± 行 0、行 3 ± 行 1）と相機の面（行 3：前の奥行き 0）。左右上下の面は相機を通るので、
    // 相機の後ろの大きな域（外接球が相機の近くまで来る）は相機の面でしか外と分からない。
    const auto row = [&](int index) { return MATH::Vec4{m.m[0][index], m.m[1][index], m.m[2][index], m.m[3][index]}; };
    const MATH::Vec4 r0 = row(0), r1 = row(1), r3 = row(3);
    const MATH::Vec4 planes[5] = {{r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w},
                                  {r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w},
                                  {r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w},
                                  {r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w},
                                  r3};
    bool inside = true;
    for (const MATH::Vec4& plane : planes) {
        const float length = MATH::Length(MATH::Vec3{plane.x, plane.y, plane.z});
        if (length > 0.0f &&
            (plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w) / length < -radius)
            inside = false;
    }
    if (!inside)
        return submission.settings.castShadows || submission.reportGlow ? 1u : 2u;
    // 距離 depth の 1 m は 行 1 の長さ（投影の y の倍率）× 高さ / 2 / depth 画素。
    const float depth = r3.x * center.x + r3.y * center.y + r3.z * center.z + r3.w - radius;
    if (depth <= 0.0f)
        return 0u;
    const float scale = MATH::Length(MATH::Vec3{r1.x, r1.y, r1.z});
    const float cellPixels = cellSize * scale * 0.5f * settings.viewHeight / depth;
    const float threshold = policy.lodCellPixels * (previous >= 1u ? 1.25f : 1.0f);
    return cellPixels < threshold ? 1u : 0u;
}
} // namespace HIKARI::RENDER3D::FLUID
