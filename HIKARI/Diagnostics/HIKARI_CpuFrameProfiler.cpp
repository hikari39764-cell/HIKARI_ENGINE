#include "Diagnostics/HIKARI_CpuFrameProfiler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "Core/HIKARI_FrameRate.h"
#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_MetricRegistry.h"

namespace HIKARI::CPU_PROFILE {

namespace {
using Clock = std::chrono::steady_clock;

constexpr std::size_t kMaximumDynamicPasses = 128u;
constexpr const char* kMetricOwner = "Diagnostics.CpuFrameProfiler";

// 区間 1 つ分の指標。初めて計測された時に登録する。
struct SegmentMetrics {
    METRIC::MetricHandle ms = METRIC::kInvalidMetric;
    METRIC::MetricHandle calls = METRIC::kInvalidMetric;
};

struct DynamicPassState {
    std::string name{};
    double elapsedMs = 0.0;
    uint32_t callCount = 0u;
    SegmentMetrics metrics{};
};

struct State {
    bool frameActive = false;
    bool discardActiveFrame = false;
    bool dynamicPassOverflowReported = false;
    uint64_t frameIndex = 0;
    Clock::time_point frameBegin{};
    std::array<double, static_cast<size_t>(Pass::Count)> elapsedMs{};
    std::array<uint32_t, static_cast<size_t>(Pass::Count)> callCounts{};
    std::array<SegmentMetrics, static_cast<size_t>(Pass::Count)> passMetrics{};
    std::array<DynamicPassState, kMaximumDynamicPasses> dynamicPasses{};
    uint32_t dynamicPassCount = 0u;
};

State gState{};

bool IsValidPass(Pass pass) { return static_cast<size_t>(pass) < static_cast<size_t>(Pass::Count); }

void Record(Pass pass, Clock::duration elapsed) {
    if (!gState.frameActive || !IsValidPass(pass) || pass == Pass::Frame) {
        return;
    }
    const size_t index = static_cast<size_t>(pass);
    gState.elapsedMs[index] += std::chrono::duration<double, std::milli>(elapsed).count();
    ++gState.callCounts[index];
}

void RecordDynamic(DynamicPassHandle pass, Clock::duration elapsed) {
    if (!gState.frameActive || pass >= gState.dynamicPassCount) {
        return;
    }
    DynamicPassState& state = gState.dynamicPasses[pass];
    state.elapsedMs += std::chrono::duration<double, std::milli>(elapsed).count();
    ++state.callCount;
}

// 実行された区間だけを出す。標本窓は「実行された回の値」だけを持つ。
void PublishSegment(SegmentMetrics& metrics, std::string_view name, METRIC::MetricCategory category, double elapsedMs,
                    uint32_t callCount) {
    if (callCount == 0u) {
        return;
    }
    if (metrics.ms == METRIC::kInvalidMetric) {
        const std::string base = "cpu." + std::string(name);
        metrics.ms = METRIC::Register(
            {base + ".ms", kMetricOwner, "ms", category, "CPU time of the scope in the frame, summed over its calls."});
        metrics.calls = METRIC::Register({base + ".calls", kMetricOwner, "count", METRIC::MetricCategory::Count,
                                          "Calls of the scope in the frame."});
    }
    METRIC::Publish(metrics.ms, elapsedMs, gState.frameIndex);
    METRIC::Publish(metrics.calls, callCount, gState.frameIndex);
}

// 待ち時間の区間（Present と GPU のフェンス待ち）は Wait、そのほかは Work。
METRIC::MetricCategory CategoryOf(Pass pass) {
    return pass == Pass::Present || pass == Pass::FenceWait ? METRIC::MetricCategory::Wait
                                                            : METRIC::MetricCategory::Work;
}

void PublishFrameMetrics(double frameMs) {
    static const METRIC::MetricHandle cpuMs = METRIC::Register(
        {METRIC::kFrameCpuMs, kMetricOwner, "ms", METRIC::MetricCategory::Work, "CPU time of the whole frame."});
    METRIC::Publish(cpuMs, frameMs, gState.frameIndex);
}
} // namespace

const char* ToString(Pass pass) {
    switch (pass) {
    case Pass::Frame: return "CPU Frame";
    case Pass::ServicesBeginFrame: return "Services BeginFrame";
    case Pass::AppUpdate: return "App Update";
    case Pass::AppRender: return "App Render";
    case Pass::AppImGui: return "App ImGui Build";
    case Pass::RenderSubmission: return "Render Submission";
    case Pass::ShadowPrepare: return "Shadow Prepare";
    case Pass::ShadowSourceSync: return "Shadow Source Sync";
    case Pass::MeshPrepare: return "Mesh Prepare";
    case Pass::MaterialPrepare: return "Material Prepare";
    case Pass::PresentationComposition: return "Presentation Composition";
    case Pass::UiLayers: return "UI Layers";
    case Pass::ImGui: return "ImGui Render";
    case Pass::CommandSubmit: return "Command Submit";
    case Pass::Present: return "Present";
    case Pass::FenceWait: return "Fence Wait";
    case Pass::Count:
    default: return "";
    }
}

void BeginFrame(uint64_t frameIndex, double previousFrameSeconds) {
    static const METRIC::MetricHandle fps =
        METRIC::Register({METRIC::kFrameFps, kMetricOwner, "fps", METRIC::MetricCategory::Count,
                          "Frames per second at the start of the frame."});
    if (const auto framesPerSecond = PERFORMANCE::FramesPerSecond(1u, previousFrameSeconds)) {
        METRIC::Publish(fps, *framesPerSecond, frameIndex);
    }
    gState.frameActive = true;
    gState.frameIndex = frameIndex;
    gState.frameBegin = Clock::now();
    gState.elapsedMs.fill(0.0);
    gState.callCounts.fill(0u);
    for (uint32_t index = 0u; index < gState.dynamicPassCount; ++index) {
        gState.dynamicPasses[index].elapsedMs = 0.0;
        gState.dynamicPasses[index].callCount = 0u;
    }
}

void EndFrame() {
    if (!gState.frameActive) {
        return;
    }

    gState.frameActive = false;
    if (gState.discardActiveFrame) {
        gState.discardActiveFrame = false;
        return;
    }

    const Clock::time_point frameEnd = Clock::now();
    const size_t framePassIndex = static_cast<size_t>(Pass::Frame);
    gState.elapsedMs[framePassIndex] = std::chrono::duration<double, std::milli>(frameEnd - gState.frameBegin).count();
    gState.callCounts[framePassIndex] = 1u;

    PublishFrameMetrics(gState.elapsedMs[framePassIndex]);
    for (size_t index = 0; index < gState.passMetrics.size(); ++index) {
        const Pass pass = static_cast<Pass>(index);
        if (pass != Pass::Frame) {
            PublishSegment(gState.passMetrics[index], ToString(pass), CategoryOf(pass), gState.elapsedMs[index],
                           gState.callCounts[index]);
        }
    }
    for (uint32_t index = 0u; index < gState.dynamicPassCount; ++index) {
        DynamicPassState& state = gState.dynamicPasses[index];
        PublishSegment(state.metrics, state.name, METRIC::MetricCategory::Work, state.elapsedMs, state.callCount);
    }
    // 診断（環境変数 HIKARI_SPIKE_TRACE=閾値ms、既定 40）：CPU の 1 フレームか、前のフレームの終わりからの間隔が
    // 閾値を超えたら、そのフレームで長かった計測を多い順に出す（突然の引っかかりの原因を探す）。
    static const double spikeThreshold = [] {
        const char* value = std::getenv("HIKARI_SPIKE_TRACE");
        return value == nullptr ? -1.0 : (std::atof(value) > 0.0 ? std::atof(value) : 40.0);
    }();
    static Clock::time_point previousEnd{};
    const double intervalMs = previousEnd == Clock::time_point{} ? 0.0
                              : std::chrono::duration<double, std::milli>(frameEnd - previousEnd).count();
    previousEnd = frameEnd;
    const double cpuMs = gState.elapsedMs[framePassIndex];
    if (spikeThreshold > 0.0 && (cpuMs > spikeThreshold || intervalMs > spikeThreshold)) {
        std::vector<std::pair<double, const char*>> items;
        for (size_t index = 0; index < gState.elapsedMs.size(); ++index) {
            if (index != framePassIndex && gState.elapsedMs[index] > 1.0) {
                items.emplace_back(gState.elapsedMs[index], ToString(static_cast<Pass>(index)));
            }
        }
        for (uint32_t index = 0u; index < gState.dynamicPassCount; ++index) {
            if (gState.dynamicPasses[index].elapsedMs > 1.0) {
                items.emplace_back(gState.dynamicPasses[index].elapsedMs, gState.dynamicPasses[index].name.c_str());
            }
        }
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        std::fprintf(stderr, "[Spike] frame=%llu cpu=%.1fms interval=%.1fms", static_cast<unsigned long long>(gState.frameIndex),
                     cpuMs, intervalMs);
        for (size_t index = 0; index < items.size() && index < 12u; ++index) {
            std::fprintf(stderr, " %s=%.1f", items[index].second, items[index].first);
        }
        std::fprintf(stderr, "\n");
    }
}

void ResetStatistics() {
    gState.discardActiveFrame = gState.frameActive;
    METRIC::ClearOwner(kMetricOwner);
}

DynamicPassHandle RegisterDynamicPass(std::string_view name) {
    if (name.empty()) {
        return kInvalidDynamicPassHandle;
    }
    for (uint32_t index = 0u; index < gState.dynamicPassCount; ++index) {
        if (gState.dynamicPasses[index].name == name) {
            return index;
        }
    }
    if (gState.dynamicPassCount >= kMaximumDynamicPasses) {
        // 溢れた計時区間は計られない。黙って捨てず、最初の一回だけ知らせる。
        if (!gState.dynamicPassOverflowReported) {
            gState.dynamicPassOverflowReported = true;
            HIKARI_LOG_WARN("[CpuFrameProfiler] all " + std::to_string(kMaximumDynamicPasses) +
                            " dynamic timing segments are in use; '" + std::string(name) +
                            "' and later new segments are not timed.");
        }
        return kInvalidDynamicPassHandle;
    }
    const DynamicPassHandle handle = gState.dynamicPassCount++;
    gState.dynamicPasses[handle].name.assign(name);
    return handle;
}

ScopedCpuTimer::ScopedCpuTimer(Pass pass)
    : pass_(pass), begin_(Clock::now()), active_(gState.frameActive && IsValidPass(pass)) {}

ScopedCpuTimer::~ScopedCpuTimer() {
    if (active_) {
        Record(pass_, Clock::now() - begin_);
    }
}

ScopedDynamicCpuTimer::ScopedDynamicCpuTimer(DynamicPassHandle pass)
    : pass_(pass), begin_(Clock::now()), active_(gState.frameActive && pass < gState.dynamicPassCount) {}

ScopedDynamicCpuTimer::~ScopedDynamicCpuTimer() {
    if (active_) {
        RecordDynamic(pass_, Clock::now() - begin_);
    }
}

} // namespace HIKARI::CPU_PROFILE
