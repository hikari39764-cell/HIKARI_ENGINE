#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <json.hpp>

namespace HIKARI::DIAGNOSTICS {

struct BenchmarkTiming {
    std::string_view name;
    double milliseconds = 0.0;
};

// 明示的な診断実行だけが所有する有界サンプル。通常のフレームには割り当てない。
class BenchmarkRun final {
public:
    BenchmarkRun(uint32_t warmupFrames, uint32_t sampleFrames);
    bool RecordFrame(uint64_t frameId, double intervalMs,
        std::span<const BenchmarkTiming> cpuPasses);
    bool RecordGpu(uint64_t frameId, std::span<const BenchmarkTiming> passes);
    bool CaptureComplete() const noexcept;
    bool GpuCoverageComplete() const noexcept;
    uint32_t SampleCount() const noexcept;
    nlohmann::json BuildReport() const;

private:
    struct CapturedFrame {
        uint64_t id = 0;
        bool gpuObserved = false;
    };
    using Series = std::map<std::string, std::vector<double>, std::less<>>;
    void RecordPasses(Series& destination, std::span<const BenchmarkTiming> passes);

    uint32_t warmupFrames_ = 0;
    uint32_t sampleFrames_ = 1;
    uint32_t warmedFrames_ = 0;
    uint32_t gpuFrames_ = 0;
    uint32_t invalidIntervals_ = 0;
    uint32_t rejectedPasses_ = 0;
    uint64_t lastFrameId_ = 0;
    bool hasFrame_ = false;
    std::vector<CapturedFrame> frames_;
    std::vector<double> intervals_;
    Series cpuPasses_;
    Series gpuPasses_;
};

} // namespace HIKARI::DIAGNOSTICS
