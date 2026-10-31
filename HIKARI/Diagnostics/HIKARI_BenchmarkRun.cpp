#include "Diagnostics/HIKARI_BenchmarkRun.h"

#include "Core/HIKARI_FrameRate.h"
#include "Diagnostics/HIKARI_PerformanceAnalysis.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace HIKARI::DIAGNOSTICS {
namespace {

constexpr size_t kMaximumPasses = 192;

nlohmann::json Summarize(const std::vector<double>& samples) {
    nlohmann::json result = {{"samples", samples.size()}, {"meanMs", nullptr}, {"p50Ms", nullptr},
                             {"p95Ms", nullptr},          {"p99Ms", nullptr},  {"maximumMs", nullptr}};
    if (samples.empty())
        return result;
    auto sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    const auto percentile = [&](double rank) {
        return sorted[static_cast<size_t>(std::ceil(rank * sorted.size())) - 1];
    };
    result["meanMs"] = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();
    result["p50Ms"] = percentile(0.50);
    result["p95Ms"] = percentile(0.95);
    result["p99Ms"] = percentile(0.99);
    result["maximumMs"] = sorted.back();
    return result;
}

double Mean(const std::vector<double>& samples) {
    return samples.empty() ? 0.0 : std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size();
}

} // namespace

BenchmarkRun::BenchmarkRun(uint32_t warmupFrames, uint32_t sampleFrames)
    : warmupFrames_((std::min)(warmupFrames, kMaximumCaptureFrames)),
      sampleFrames_((std::clamp)(sampleFrames, 1u, kMaximumCaptureFrames)) {
    frames_.reserve(sampleFrames_);
    intervals_.reserve(sampleFrames_);
    timings_.reserve(kMaximumPasses);
}

// 登録表の id は重複しないので、同じフレームで同名の区間が二重に入ることはない。入力名も容量も制限する。
void BenchmarkRun::RecordPasses(Series& destination, std::span<const Timing> passes) {
    for (size_t i = 0; i < (std::min)(passes.size(), kMaximumPasses); ++i) {
        const auto& pass = passes[i];
        if (pass.name.empty() || pass.name.size() > 128 || !std::isfinite(pass.milliseconds) ||
            pass.milliseconds < 0.0) {
            ++rejectedPasses_;
            continue;
        }
        auto found = destination.find(pass.name);
        if (found == destination.end()) {
            if (destination.size() >= kMaximumPasses) {
                ++rejectedPasses_;
                continue;
            }
            found = destination.emplace(std::string(pass.name), PassSeries{}).first;
            found->second.module = pass.module;
            found->second.category = pass.category;
            found->second.ms.reserve(sampleFrames_);
        }
        found->second.ms.push_back(pass.milliseconds);
        if (pass.detailed) {
            found->second.selfMs.push_back(pass.selfMs);
            found->second.calls.push_back(pass.calls);
        }
    }
    rejectedPasses_ += static_cast<uint32_t>(passes.size() - (std::min)(passes.size(), kMaximumPasses));
}

void BenchmarkRun::CollectTimings(const std::vector<METRIC::MetricReading>& metrics, std::string_view domain,
                                  std::string_view frameMetricId, std::string_view frameName, uint64_t sourceFrame) {
    const auto observed = [sourceFrame](const METRIC::MetricReading* reading) {
        return reading != nullptr && reading->status == METRIC::MetricStatus::Fresh &&
               reading->sourceFrame == sourceFrame;
    };
    timings_.clear();
    if (const METRIC::MetricReading* frame = METRIC::FindReading(metrics, frameMetricId); observed(frame)) {
        timings_.push_back(
            {frameName, frame->descriptor->module, METRIC::ToString(frame->descriptor->category), frame->value});
    }
    for (const METRIC::TimingRow& row : METRIC::TimingRows(metrics, domain)) {
        if (observed(row.ms)) {
            const METRIC::MetricDescriptor& descriptor = *row.ms->descriptor;
            timings_.push_back({row.name, descriptor.module, METRIC::ToString(descriptor.category), row.ms->value,
                                row.selfMs != nullptr ? row.selfMs->value : 0.0,
                                row.calls != nullptr ? row.calls->value : 0.0,
                                row.selfMs != nullptr && row.calls != nullptr});
        }
    }
}

bool BenchmarkRun::RecordFrame(uint64_t frameId, double intervalMs, const std::vector<METRIC::MetricReading>& metrics) {
    if (CaptureComplete() || (hasFrame_ && frameId <= lastFrameId_))
        return false;
    hasFrame_ = true;
    lastFrameId_ = frameId;
    if (warmedFrames_ < warmupFrames_) {
        ++warmedFrames_;
        return false;
    }
    frames_.push_back({frameId, false});
    if (std::isfinite(intervalMs) && intervalMs > 0.0)
        intervals_.push_back(intervalMs);
    else
        ++invalidIntervals_;
    CollectTimings(metrics, "cpu", METRIC::kFrameCpuMs, "CPU Frame", frameId);
    RecordPasses(cpuPasses_, timings_);
    return true;
}

bool BenchmarkRun::RecordGpu(const std::vector<METRIC::MetricReading>& metrics) {
    const METRIC::MetricReading* gpuFrame = METRIC::FindReading(metrics, METRIC::kFrameGpuMs);
    if (gpuFrame == nullptr || gpuFrame->status != METRIC::MetricStatus::Fresh)
        return false;
    const uint64_t frameId = gpuFrame->sourceFrame;
    auto found = std::lower_bound(frames_.begin(), frames_.end(), frameId,
                                  [](const CapturedFrame& frame, uint64_t id) { return frame.id < id; });
    if (found == frames_.end() || found->id != frameId || found->gpuObserved)
        return false;
    found->gpuObserved = true;
    ++gpuFrames_;
    CollectTimings(metrics, "gpu", METRIC::kFrameGpuMs, "Command Frame", frameId);
    RecordPasses(gpuPasses_, timings_);
    return true;
}

bool BenchmarkRun::WarmupComplete() const noexcept { return warmedFrames_ >= warmupFrames_; }
uint32_t BenchmarkRun::WarmedFrameCount() const noexcept { return warmedFrames_; }
bool BenchmarkRun::CaptureComplete() const noexcept { return frames_.size() == sampleFrames_; }
bool BenchmarkRun::GpuCoverageComplete() const noexcept { return CaptureComplete() && gpuFrames_ == sampleFrames_; }
uint32_t BenchmarkRun::SampleCount() const noexcept { return static_cast<uint32_t>(frames_.size()); }

nlohmann::json BenchmarkRun::BuildModules() const {
    struct Total {
        double cpuSelfMs = 0.0;
        double cpuWaitMs = 0.0;
        double gpuSelfMs = 0.0;
        uint32_t cpuScopes = 0;
        uint32_t gpuPasses = 0;
    };
    std::map<std::string, Total> totals;
    const auto add = [&totals](const Series& series, size_t frames, bool cpu) {
        for (const auto& [name, pass] : series) {
            if (pass.selfMs.empty() || frames == 0u)
                continue;
            Total& total = totals[pass.module];
            const double perFrame = std::accumulate(pass.selfMs.begin(), pass.selfMs.end(), 0.0) / frames;
            if (!cpu) {
                total.gpuSelfMs += perFrame;
                ++total.gpuPasses;
            } else {
                (pass.category == METRIC::ToString(METRIC::MetricCategory::Wait) ? total.cpuWaitMs : total.cpuSelfMs) +=
                    perFrame;
                ++total.cpuScopes;
            }
        }
    };
    add(cpuPasses_, frames_.size(), true);
    add(gpuPasses_, gpuFrames_, false);
    nlohmann::json modules = nlohmann::json::object();
    for (const auto& [module, total] : totals) {
        modules[module] = {{"cpuSelfMs", total.cpuSelfMs},
                           {"cpuWaitMs", total.cpuWaitMs},
                           {"gpuSelfMs", total.gpuSelfMs},
                           {"cpuScopes", total.cpuScopes},
                           {"gpuPasses", total.gpuPasses}};
    }
    return modules;
}

nlohmann::json BenchmarkRun::BuildReport() const {
    nlohmann::json report = {
        {"schemaVersion", kCaptureSchemaVersion},
        {"captureComplete", CaptureComplete()},
        {"warmupFramesRequested", warmupFrames_},
        {"warmupFramesObserved", warmedFrames_},
        {"sampleFramesRequested", sampleFrames_},
        {"sampleFramesObserved", frames_.size()},
        {"gpuFramesObserved", gpuFrames_},
        {"gpuCoverageComplete", GpuCoverageComplete()},
        {"invalidIntervals", invalidIntervals_},
        {"rejectedPassSamples", rejectedPasses_},
        {"frameInterval", Summarize(intervals_)},
        {"renderedFps", nullptr},
        {"cpuPasses", nlohmann::json::object()},
        {"gpuPasses", nlohmann::json::object()},
        {"modules", BuildModules()},
        {"timingSemantics",
         "Frame intervals are host presents, excluding generated frames. Pass meanMs and the percentiles cover the "
         "frames in which the pass ran and include nested passes, so they must not be added; selfMeanMs excludes "
         "nested passes and the modules block adds it up per frame (per-frame averages over the whole capture): "
         "cpuSelfMs for CPU work, cpuWaitMs for scopes that only wait (present, fence), gpuSelfMs for GPU passes. "
         "Percentiles use nearest rank over this capture only."}};
    const double elapsedMs = std::accumulate(intervals_.begin(), intervals_.end(), 0.0);
    if (const auto fps = PERFORMANCE::FramesPerSecond(intervals_.size(), elapsedMs / 1000.0))
        report["renderedFps"] = *fps;
    for (const double threshold : {33.333333, 50.0, 100.0, 250.0}) {
        report["longFrames"].push_back(
            {{"thresholdMs", threshold},
             {"count", std::count_if(intervals_.begin(), intervals_.end(),
                                     [threshold](double value) { return value > threshold; })}});
    }
    const auto describe = [](const PassSeries& pass) {
        nlohmann::json entry = Summarize(pass.ms);
        entry["module"] = pass.module;
        entry["category"] = pass.category;
        if (!pass.selfMs.empty()) {
            entry["selfMeanMs"] = Mean(pass.selfMs);
            entry["callsMean"] = Mean(pass.calls);
            entry["callsMax"] = *std::max_element(pass.calls.begin(), pass.calls.end());
        }
        return entry;
    };
    for (const auto& [name, pass] : cpuPasses_)
        report["cpuPasses"][name] = describe(pass);
    for (const auto& [name, pass] : gpuPasses_)
        report["gpuPasses"][name] = describe(pass);
    if (!frames_.empty()) {
        report["firstFrameId"] = frames_.front().id;
        report["lastFrameId"] = frames_.back().id;
    }
    return report;
}

} // namespace HIKARI::DIAGNOSTICS
