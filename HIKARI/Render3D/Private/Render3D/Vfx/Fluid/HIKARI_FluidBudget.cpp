#include "Render3D/Vfx/Fluid/HIKARI_FluidDomainState.h"

#include <algorithm>

#include "Gfx/HIKARI_GpuFrameProfiler.h"

// 流体の解算の予算：域ごとの 1 フレームの見積もりと、重要でない域から子步を長くし、それでも超えれば止める割り振り。
namespace HIKARI::RENDER3D::FLUID::INTERNAL {

double EstimateFluidSolveCost(const FluidGridLayout& layout, float activeFraction, float pressureFraction,
                              double frameSeconds, double stepSeconds, uint32_t maxSubsteps,
                              uint32_t vCycles) noexcept {
    if (!(stepSeconds > 0.0) || !(frameSeconds > 0.0))
        return 0.0;
    const double steps = (std::min)(frameSeconds / stepSeconds, static_cast<double>(maxSubsteps));
    const double cells = static_cast<double>(layout.cells[0]) * layout.cells[1] * layout.cells[2] * 1.0e-6;
    return cells * steps *
           (2.0 * std::clamp(activeFraction, 0.0f, 1.0f) + 0.5 * vCycles * std::clamp(pressureFraction, 0.0f, 1.0f));
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
    // 合計が limit 以下になるまで、最も重要でない域から段を上げる。
    const auto coarsen = [&entries, &total](double limit) noexcept {
        for (size_t index = entries.size(); total > limit && index-- > 0u;) {
            FluidBudgetEntry& entry = entries[index];
            for (; !entry.frozen && entry.level < 2u && total > limit; ++entry.level)
                total -= entry.cost[entry.level] - entry.cost[entry.level + 1u];
        }
    };
    coarsen(budget);
    for (size_t index = entries.size(); total > budget && index-- > 1u;) {
        FluidBudgetEntry& entry = entries[index];
        if (!entry.frozen) {
            total -= entry.cost[entry.level];
            entry.frozen = true;
        }
    }
    // 止めた域を戻すのを、段を下げるより先にする。重要な域が先に段を下げて余裕を使い切ると、止めた域は入る所が無くなり、
    // 寿命が尽きるまで固まったまま描かれる。
    const double roomy = 0.85 * budget;
    for (FluidBudgetEntry& entry : entries) {
        if (!entry.frozen)
            continue;
        const double limit = entry.frozenFrames >= kFluidFrozenPatienceFrames ? budget : roomy;
        double coarsest = entry.cost[entry.level];
        for (const FluidBudgetEntry& other : entries)
            coarsest += other.frozen ? 0.0 : other.cost[2];
        if (coarsest > limit)
            continue;
        coarsen(limit - entry.cost[entry.level]);
        total += entry.cost[entry.level];
        entry.frozen = false;
    }
    if (total >= 0.7 * budget)
        return;
    for (FluidBudgetEntry& entry : entries) {
        for (; !entry.frozen && entry.level > 0u &&
               total + entry.cost[entry.level - 1u] - entry.cost[entry.level] <= roomy;
             --entry.level)
            total += entry.cost[entry.level - 1u] - entry.cost[entry.level];
    }
}

double FluidPlanSeconds::Scale(uint64_t frameIndex, double frameSeconds) noexcept {
    if (frameIndex == frame_)
        return scale_;
    frame_ = frameIndex;
    scale_ = 1.0;
    if (!(frameSeconds > 0.0))
        return scale_;
    recent_[count_ % recent_.size()] = frameSeconds;
    ++count_;
    std::array<double, 5> sorted = recent_;
    const size_t filled = (std::min)(static_cast<size_t>(count_), sorted.size());
    std::sort(sorted.begin(), sorted.begin() + filled);
    // 偶数個の間（最初の数フレーム）は小さい方の中央を取る。
    const double median = sorted[(filled - 1u) / 2u];
    if (frameSeconds > median)
        scale_ = median / frameSeconds;
    return scale_;
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
