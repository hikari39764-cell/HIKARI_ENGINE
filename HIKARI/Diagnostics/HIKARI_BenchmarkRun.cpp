#include "Diagnostics/HIKARI_BenchmarkRun.h"

#include "Core/HIKARI_FrameRate.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace HIKARI::DIAGNOSTICS {
namespace {

constexpr uint32_t kMaximumFrames = 36000;
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

} // namespace

BenchmarkRun::BenchmarkRun(uint32_t warmupFrames, uint32_t sampleFrames)
    : warmupFrames_((std::min)(warmupFrames, kMaximumFrames)),
      sampleFrames_((std::clamp)(sampleFrames, 1u, kMaximumFrames)) {
    frames_.reserve(sampleFrames_);
    intervals_.reserve(sampleFrames_);
}

void BenchmarkRun::RecordPasses(Series& destination, std::span<const BenchmarkTiming> passes) {
    // 同じフレームで同名のパスを二重計上しない。入力名も容量も制限する。
    for (size_t i = 0; i < (std::min)(passes.size(), kMaximumPasses); ++i) {
        const auto& pass = passes[i];
        if (pass.name.empty() || pass.name.size() > 128 || !std::isfinite(pass.milliseconds) ||
            pass.milliseconds < 0.0) {
            ++rejectedPasses_;
            continue;
        }
        bool duplicate = false;
        for (size_t earlier = 0; earlier < i; ++earlier)
            duplicate |= passes[earlier].name == pass.name;
        if (duplicate) {
            ++rejectedPasses_;
            continue;
        }
        auto found = destination.find(pass.name);
        if (found == destination.end()) {
            if (destination.size() >= kMaximumPasses) {
                ++rejectedPasses_;
                continue;
            }
            found = destination.emplace(std::string(pass.name), std::vector<double>{}).first;
            found->second.reserve(sampleFrames_);
        }
        found->second.push_back(pass.milliseconds);
    }
    rejectedPasses_ += static_cast<uint32_t>(passes.size() - (std::min)(passes.size(), kMaximumPasses));
}

bool BenchmarkRun::RecordFrame(uint64_t frameId, double intervalMs, std::span<const BenchmarkTiming> cpuPasses) {
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
    RecordPasses(cpuPasses_, cpuPasses);
    return true;
}

bool BenchmarkRun::RecordGpu(uint64_t frameId, std::span<const BenchmarkTiming> passes) {
    auto found = std::lower_bound(frames_.begin(), frames_.end(), frameId,
                                  [](const CapturedFrame& frame, uint64_t id) { return frame.id < id; });
    if (found == frames_.end() || found->id != frameId || found->gpuObserved)
        return false;
    found->gpuObserved = true;
    ++gpuFrames_;
    RecordPasses(gpuPasses_, passes);
    return true;
}

bool BenchmarkRun::CaptureComplete() const noexcept { return frames_.size() == sampleFrames_; }
bool BenchmarkRun::GpuCoverageComplete() const noexcept { return CaptureComplete() && gpuFrames_ == sampleFrames_; }
uint32_t BenchmarkRun::SampleCount() const noexcept { return static_cast<uint32_t>(frames_.size()); }

nlohmann::json BenchmarkRun::BuildReport() const {
    nlohmann::json report = {
        {"schemaVersion", 1},
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
        {"timingSemantics",
         "Frame intervals are host presents, excluding generated frames. CPU/GPU passes may overlap and must not be added as frame time. Percentiles use nearest rank over this capture only."}};
    const double elapsedMs = std::accumulate(intervals_.begin(), intervals_.end(), 0.0);
    if (const auto fps = PERFORMANCE::FramesPerSecond(intervals_.size(), elapsedMs / 1000.0))
        report["renderedFps"] = *fps;
    for (const double threshold : {33.333333, 50.0, 100.0, 250.0}) {
        report["longFrames"].push_back(
            {{"thresholdMs", threshold},
             {"count", std::count_if(intervals_.begin(), intervals_.end(),
                                     [threshold](double value) { return value > threshold; })}});
    }
    for (const auto& [name, series] : cpuPasses_)
        report["cpuPasses"][name] = Summarize(series);
    for (const auto& [name, series] : gpuPasses_)
        report["gpuPasses"][name] = Summarize(series);
    if (!frames_.empty()) {
        report["firstFrameId"] = frames_.front().id;
        report["lastFrameId"] = frames_.back().id;
    }
    return report;
}

} // namespace HIKARI::DIAGNOSTICS
