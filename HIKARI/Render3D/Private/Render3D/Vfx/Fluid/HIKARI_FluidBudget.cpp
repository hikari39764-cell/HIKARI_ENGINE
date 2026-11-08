#include "Render3D/Vfx/Fluid/HIKARI_FluidDomainState.h"

#include <algorithm>

#include "Gfx/HIKARI_GpuFrameProfiler.h"

// 流体の解算の予算：域ごとの 1 フレームの見積もりと、重要でない域から子步を長くし、それでも超えれば止める割り振り。
namespace HIKARI::RENDER3D::FLUID::INTERNAL {

double EstimateFluidSolveCost(const FluidGridLayout& layout, float activeFraction, double frameSeconds,
                              double stepSeconds, uint32_t maxSubsteps, uint32_t vCycles) noexcept {
    if (!(stepSeconds > 0.0) || !(frameSeconds > 0.0))
        return 0.0;
    const double steps = (std::min)(frameSeconds / stepSeconds, static_cast<double>(maxSubsteps));
    const double cells = static_cast<double>(layout.cells[0]) * layout.cells[1] * layout.cells[2] * 1.0e-6;
    return cells * steps * (2.0 * std::clamp(activeFraction, 0.0f, 1.0f) + 0.5 * vCycles);
}

void AssignFluidBudget(std::span<FluidBudgetEntry> entries, double budget) noexcept {
    if (!(budget > 0.0)) {
        for (FluidBudgetEntry& entry : entries) {
            entry.level = 0u;
            entry.frozen = false;
        }
        return;
    }
    double total = 0.0;
    for (const FluidBudgetEntry& entry : entries)
        total += entry.frozen ? 0.0 : entry.cost[entry.level];
    for (size_t index = entries.size(); total > budget && index-- > 0u;) {
        FluidBudgetEntry& entry = entries[index];
        for (; !entry.frozen && entry.level < 2u && total > budget; ++entry.level)
            total -= entry.cost[entry.level] - entry.cost[entry.level + 1u];
    }
    for (size_t index = entries.size(); total > budget && index-- > 1u;) {
        FluidBudgetEntry& entry = entries[index];
        if (!entry.frozen) {
            total -= entry.cost[entry.level];
            entry.frozen = true;
        }
    }
    if (total >= 0.7 * budget)
        return;
    const double roomy = 0.85 * budget;
    for (FluidBudgetEntry& entry : entries) {
        if (entry.frozen && total + entry.cost[entry.level] <= roomy) {
            total += entry.cost[entry.level];
            entry.frozen = false;
        }
        for (; !entry.frozen && entry.level > 0u &&
               total + entry.cost[entry.level - 1u] - entry.cost[entry.level] <= roomy;
             --entry.level)
            total += entry.cost[entry.level - 1u] - entry.cost[entry.level];
    }
}

void FluidBudgetCalibration::Record(uint64_t frameIndex, double units, bool unmodeled) noexcept {
    frames_[frameIndex % frames_.size()] = {frameIndex, units, unmodeled};
}

void FluidBudgetCalibration::Update() {
    const GFX::GPU_PROFILE::PassReading reading =
        GFX::GPU_PROFILE::LastPassReading(GFX::GPU_PROFILE::Pass::FluidSimulation);
    if (reading.frameIndex == 0u || reading.frameIndex == measuredFrame_)
        return;
    measuredFrame_ = reading.frameIndex;
    // 見積もりがごく小さいフレーム（烟の少ない域だけ）は計時の端数が勝つので組にしない。
    const Frame& frame = frames_[reading.frameIndex % frames_.size()];
    if (frame.index != reading.frameIndex || frame.unmodeled || frame.units < 0.05 || !(reading.ms > 0.0))
        return;
    ms_ += reading.ms;
    units_ += frame.units;
    if (++pairs_ < kFluidCalibrationFrames)
        return;
    const double measured = ms_ / units_;
    msPerUnit_ = std::clamp(std::clamp(0.5 * (msPerUnit_ + measured), 0.5 * msPerUnit_, 2.0 * msPerUnit_), 0.05, 20.0);
    ms_ = 0.0;
    units_ = 0.0;
    pairs_ = 0u;
}

} // namespace HIKARI::RENDER3D::FLUID::INTERNAL
