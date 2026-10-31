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

// 1 回の捕獲で採れる採取フレーム数の上限（捕獲を始める側の引数の範囲もこれに合わせる）。
inline constexpr uint32_t kMaximumCaptureFrames = 36000u;

// 性能捕獲の採取部分：熱身のあとの有界サンプルを持ち、キー順の決まった JSON 摘要にする。通常のフレームには割り当てない。
// CPU / GPU の区間は登録表（METRIC::Snapshot）から読み、来源フレームが一致するものだけを記録する。
// 区間ごとに耗時・self 時間（入れ子の子を除く）・呼び出し回数・所属モジュールを持ち、モジュール別にも集計する。
class BenchmarkRun final {
public:
    BenchmarkRun(uint32_t warmupFrames, uint32_t sampleFrames);
    // frameId の CPU 区間を記録する。frameId は登録表の cpu.* の来源フレームと同じ番号。
    bool RecordFrame(uint64_t frameId, double intervalMs, const std::vector<METRIC::MetricReading>& metrics);
    // GPU の計時は読み戻しで遅れて届く。登録表の最新の GPU フレームを、同じ番号の記録済みフレームへ入れる。
    bool RecordGpu(const std::vector<METRIC::MetricReading>& metrics);
    // 熱身が終わった（次のフレームから採取する）。
    bool WarmupComplete() const noexcept;
    uint32_t WarmedFrameCount() const noexcept;
    bool CaptureComplete() const noexcept;
    bool GpuCoverageComplete() const noexcept;
    uint32_t SampleCount() const noexcept;
    nlohmann::json BuildReport() const;

private:
    struct CapturedFrame {
        uint64_t id = 0;
        bool gpuObserved = false;
    };
    // 登録表から取った 1 区間の 1 フレーム分。フレーム全体の行は detailed が偽（self と回数を持たない）。
    struct Timing {
        std::string_view name;
        std::string_view module;
        std::string_view category;
        double milliseconds = 0.0;
        double selfMs = 0.0;
        double calls = 0.0;
        bool detailed = false;
    };
    struct PassSeries {
        std::string module;
        std::string category;
        std::vector<double> ms;
        std::vector<double> selfMs;
        std::vector<double> calls;
    };
    using Series = std::map<std::string, PassSeries, std::less<>>;
    // domain（"cpu" か "gpu"）の区間のうち、来源フレームが sourceFrame のものを timings_ に集める。
    // フレーム全体（frameMetricId）は、基準 JSON の従来のキー frameName で先頭に入れる。
    void CollectTimings(const std::vector<METRIC::MetricReading>& metrics, std::string_view domain,
                        std::string_view frameMetricId, std::string_view frameName, uint64_t sourceFrame);
    void RecordPasses(Series& destination, std::span<const Timing> passes);
    // 区間の self 時間をモジュールごとに足す（1 フレームあたりの平均）。入れ子の分は引いてあるので足し合わせてよい。
    // 待つだけの区間（Present・フェンス待ち）は、作業の費用 cpuSelfMs ではなく cpuWaitMs へ入れる。
    nlohmann::json BuildModules() const;

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
