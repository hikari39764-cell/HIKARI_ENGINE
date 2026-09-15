#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace HIKARI::PERFORMANCE {

inline constexpr std::size_t kTimingWindowSampleCount = 300u;

// CPU/GPU 共通の標本窓。分位点は毎フレーム再計算せず、15 標本ごとに更新する。
struct RollingTimingWindow {
    std::array<double, kTimingWindowSampleCount> samples{};
    std::size_t next = 0u;
    std::size_t count = 0u;
    double sum = 0.0;
    double maximum = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    uint32_t samplesSinceQuantiles = 0u;

    double Percentile(double fraction) const {
        if (count == 0u) {
            return 0.0;
        }
        std::array<double, kTimingWindowSampleCount> ordered{};
        std::copy_n(samples.begin(), count, ordered.begin());
        const std::size_t percentileIndex = (std::min)(
            count - 1u,
            (std::max)(std::size_t{1}, static_cast<std::size_t>(std::ceil(
                std::clamp(fraction, 0.0, 1.0) * static_cast<double>(count)))) - 1u);
        std::nth_element(
            ordered.begin(),
            ordered.begin() + percentileIndex,
            ordered.begin() + count);
        return ordered[percentileIndex];
    }

    void Push(double sample) {
        const double safeSample = std::isfinite(sample)
            ? (std::max)(sample, 0.0)
            : 0.0;
        const bool full = count == samples.size();
        const double removed = full ? samples[next] : 0.0;
        const bool removedMaximum = full && removed >= maximum;
        if (full) {
            sum -= removed;
        } else {
            ++count;
        }
        samples[next] = safeSample;
        next = (next + 1u) % samples.size();
        sum += safeSample;
        if (removedMaximum) {
            maximum = 0.0;
            for (std::size_t index = 0u; index < count; ++index) {
                maximum = (std::max)(maximum, samples[index]);
            }
        } else {
            maximum = (std::max)(maximum, safeSample);
        }

        ++samplesSinceQuantiles;
        if (count == 1u || samplesSinceQuantiles >= 15u) {
            p95 = Percentile(0.95);
            p99 = Percentile(0.99);
            samplesSinceQuantiles = 0u;
        }
    }

    double Average() const noexcept {
        return count == 0u
            ? 0.0
            : (std::max)(0.0, sum / static_cast<double>(count));
    }
};

} // namespace HIKARI::PERFORMANCE
