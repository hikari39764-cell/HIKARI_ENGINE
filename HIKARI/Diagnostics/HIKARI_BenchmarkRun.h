#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <json.hpp>

#include "Diagnostics/HIKARI_MetricRegistry.h"

namespace HIKARI::DIAGNOSTICS {

// 明示的な診断実行だけが所有する有界サンプル。通常のフレームには割り当てない。
// CPU / GPU の区間耗時は登録表（METRIC::Snapshot）から読み、来源フレームが一致するものだけを記録する。
class BenchmarkRun final {
public:
    BenchmarkRun(uint32_t warmupFrames, uint32_t sampleFrames);
    // frameId の CPU 区間を記録する。frameId は登録表の cpu.* の来源フレームと同じ番号。
    bool RecordFrame(uint64_t frameId, double intervalMs, const std::vector<METRIC::MetricReading>& metrics);
    // GPU の計時は読み戻しで遅れて届く。登録表の最新の GPU フレームを、同じ番号の記録済みフレームへ入れる。
    bool RecordGpu(const std::vector<METRIC::MetricReading>& metrics);
    bool CaptureComplete() const noexcept;
    bool GpuCoverageComplete() const noexcept;
    uint32_t SampleCount() const noexcept;
    nlohmann::json BuildReport() const;

private:
    struct CapturedFrame {
        uint64_t id = 0;
        bool gpuObserved = false;
    };
    struct Timing {
        std::string_view name;
        double milliseconds = 0.0;
    };
    using Series = std::map<std::string, std::vector<double>, std::less<>>;
    // domain（"cpu" か "gpu"）の区間のうち、来源フレームが sourceFrame のものを timings_ に集める。
    // フレーム全体（frameMetricId）は、基準 JSON の従来のキー frameName で先頭に入れる。
    void CollectTimings(const std::vector<METRIC::MetricReading>& metrics, std::string_view domain,
                        std::string_view frameMetricId, std::string_view frameName, uint64_t sourceFrame);
    void RecordPasses(Series& destination, std::span<const Timing> passes);

    uint32_t warmupFrames_ = 0;
    uint32_t sampleFrames_ = 1;
    uint32_t warmedFrames_ = 0;
    uint32_t gpuFrames_ = 0;
    uint32_t invalidIntervals_ = 0;
    uint32_t rejectedPasses_ = 0;
    uint64_t lastFrameId_ = 0;
    bool hasFrame_ = false;
    std::vector<CapturedFrame> frames_;
    std::vector<Timing> timings_;
    std::vector<double> intervals_;
    Series cpuPasses_;
    Series gpuPasses_;
};

} // namespace HIKARI::DIAGNOSTICS
