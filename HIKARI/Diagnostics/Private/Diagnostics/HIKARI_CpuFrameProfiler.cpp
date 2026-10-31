#include "Diagnostics/HIKARI_CpuFrameProfiler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "Core/HIKARI_FrameRate.h"
#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_MetricRegistry.h"
#include "Diagnostics/HIKARI_TraceRecorder.h"

namespace HIKARI::CPU_PROFILE {

namespace {
using Clock = std::chrono::steady_clock;

constexpr std::size_t kMaximumDynamicPasses = 128u;
// 入れ子の区間を数える深さの上限。超えた区間は、子の時間を引かない（self = 全体）。
constexpr uint32_t kMaximumScopeDepth = 64u;
constexpr uint32_t kUntrackedDepth = UINT32_MAX;
constexpr const char* kMetricOwner = "Diagnostics.CpuFrameProfiler";

// 固定の区間の表。名前と所属モジュールを 1 行にまとめる（登録表の id と時間線の cat になる）。
struct PassInfo {
    Pass pass;
    const char* name;
    const char* module;
};
constexpr PassInfo kPassInfo[] = {
    {Pass::Frame, "CPU Frame", "Runtime"},
    {Pass::ServicesBeginFrame, "Services BeginFrame", "Runtime"},
    {Pass::AppUpdate, "App Update", "Runtime"},
    {Pass::AppRender, "App Render", "Runtime"},
    {Pass::AppImGui, "App ImGui Build", "Editor"},
    {Pass::RenderSubmission, "Render Submission", "Scene"},
    {Pass::ShadowPrepare, "Shadow Prepare", "Render3D"},
    {Pass::ShadowSourceSync, "Shadow Source Sync", "Render3D"},
    {Pass::MeshPrepare, "Mesh Prepare", "Render3D"},
    {Pass::MaterialPrepare, "Material Prepare", "Render3D"},
    {Pass::PresentationComposition, "Presentation Composition", "Runtime"},
    {Pass::UiLayers, "UI Layers", "Render2D"},
    {Pass::ImGui, "ImGui Render", "Editor"},
    {Pass::CommandSubmit, "Command Submit", "Gfx"},
    {Pass::Present, "Present", "Gfx"},
    {Pass::FenceWait, "Fence Wait", "Gfx"},
};
constexpr bool PassInfoFollowsEnum() {
    for (size_t index = 0; index < std::size(kPassInfo); ++index) {
        if (static_cast<size_t>(kPassInfo[index].pass) != index) {
            return false;
        }
    }
    return true;
}
static_assert(std::size(kPassInfo) == static_cast<size_t>(Pass::Count), "every Pass needs a row");
static_assert(PassInfoFollowsEnum(), "kPassInfo must follow the Pass order");

// 動的区間の名前は "<領域>.<区間>" か "<領域>/<区間>"。領域から所属モジュールを決める。
// 表に無い領域は、領域名がそのままモジュール名（Editor、Scene、Vfx）。
std::string ModuleOfSegment(std::string_view name) {
    struct Domain {
        std::string_view domain;
        const char* module;
    };
    static constexpr Domain kDomains[] = {
        {"Cache", "Scene"},           {"Frame", "Runtime"},   {"Graph", "Render3D"},
        {"Main", "Render3D"},         {"Render", "Render3D"}, {"Sand", "Scene"},
        {"ShadowSource", "Render3D"}, {"System", "Scene"},    {"Text", "Render3D"},
    };
    const std::string_view domain = name.substr(0, name.find_first_of("./"));
    for (const Domain& entry : kDomains) {
        if (entry.domain == domain) {
            return entry.module;
        }
    }
    return std::string(domain);
}

// 区間 1 つ分の指標。初めて計測された時に登録する。
struct SegmentMetrics {
    METRIC::MetricHandle ms = METRIC::kInvalidMetric;
    METRIC::MetricHandle calls = METRIC::kInvalidMetric;
    METRIC::MetricHandle selfMs = METRIC::kInvalidMetric;
};

struct DynamicPassState {
    std::string name{};
    std::string module{};
    double elapsedMs = 0.0;
    double selfMs = 0.0;
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
    std::array<double, static_cast<size_t>(Pass::Count)> selfMs{};
    std::array<uint32_t, static_cast<size_t>(Pass::Count)> callCounts{};
    std::array<SegmentMetrics, static_cast<size_t>(Pass::Count)> passMetrics{};
    std::array<DynamicPassState, kMaximumDynamicPasses> dynamicPasses{};
    uint32_t dynamicPassCount = 0u;
    // 入れ子の深さごとに、すでに閉じた直接の子区間の時間の合計。
    std::array<double, kMaximumScopeDepth> childMs{};
    uint32_t scopeDepth = 0u;
};

State gState{};

bool IsValidPass(Pass pass) { return static_cast<size_t>(pass) < static_cast<size_t>(Pass::Count); }

double Milliseconds(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

// 区間に入る。戻り値は閉じる時に LeaveScope へ渡す深さ。
uint32_t EnterScope() {
    if (gState.scopeDepth >= kMaximumScopeDepth) {
        return kUntrackedDepth;
    }
    gState.childMs[gState.scopeDepth] = 0.0;
    return gState.scopeDepth++;
}

// 区間を閉じ、子の時間を除いた self を返す。この区間の時間は外側の区間の「子」になる。
// 入れ子が崩れた（内側が残ったまま外側が閉じた）区間は、子を引かず self = 全体。
double LeaveScope(uint32_t depth, double elapsed) {
    if (depth == kUntrackedDepth || depth + 1u != gState.scopeDepth) {
        return elapsed;
    }
    gState.scopeDepth = depth;
    if (depth > 0u) {
        gState.childMs[depth - 1u] += elapsed;
    }
    return (std::max)(0.0, elapsed - gState.childMs[depth]);
}

void Record(Pass pass, Clock::time_point begin, Clock::time_point end, uint32_t depth) {
    if (!gState.frameActive || !IsValidPass(pass) || pass == Pass::Frame) {
        return;
    }
    const size_t index = static_cast<size_t>(pass);
    const double elapsed = Milliseconds(begin, end);
    gState.elapsedMs[index] += elapsed;
    gState.selfMs[index] += LeaveScope(depth, elapsed);
    ++gState.callCounts[index];
    if (TRACING::IsRecording()) {
        TRACING::RecordCpu(kPassInfo[index].name, kPassInfo[index].module, begin, end, gState.frameIndex);
    }
}

void RecordDynamic(DynamicPassHandle pass, Clock::time_point begin, Clock::time_point end, uint32_t depth) {
    if (!gState.frameActive || pass >= gState.dynamicPassCount) {
        return;
    }
    DynamicPassState& state = gState.dynamicPasses[pass];
    const double elapsed = Milliseconds(begin, end);
    state.elapsedMs += elapsed;
    state.selfMs += LeaveScope(depth, elapsed);
    ++state.callCount;
    if (TRACING::IsRecording()) {
        TRACING::RecordCpu(state.name.c_str(), state.module.c_str(), begin, end, gState.frameIndex);
    }
}

// 実行された区間だけを出す。標本窓は「実行された回の値」だけを持つ。
void PublishSegment(SegmentMetrics& metrics, std::string_view name, std::string_view module,
                    METRIC::MetricCategory category, double elapsedMs, double selfMs, uint32_t callCount) {
    if (callCount == 0u) {
        return;
    }
    if (metrics.ms == METRIC::kInvalidMetric) {
        const std::string base = "cpu." + std::string(name);
        metrics.ms = METRIC::Register({base + ".ms", kMetricOwner, std::string(module), "ms", category,
                                       "CPU time of the scope in the frame, summed over its calls."});
        metrics.calls = METRIC::Register({base + ".calls", kMetricOwner, std::string(module), "count",
                                          METRIC::MetricCategory::Count, "Calls of the scope in the frame."});
        metrics.selfMs = METRIC::Register({base + ".selfMs", kMetricOwner, std::string(module), "ms", category,
                                           "CPU time of the scope without the scopes nested inside it."});
    }
    METRIC::Publish(metrics.ms, elapsedMs, gState.frameIndex);
    METRIC::Publish(metrics.calls, callCount, gState.frameIndex);
    METRIC::Publish(metrics.selfMs, selfMs, gState.frameIndex);
}

// 待ち時間の区間（Present と GPU のフェンス待ち）は Wait、そのほかは Work。
METRIC::MetricCategory CategoryOf(Pass pass) {
    return pass == Pass::Present || pass == Pass::FenceWait ? METRIC::MetricCategory::Wait
                                                            : METRIC::MetricCategory::Work;
}

void PublishFrameMetrics(double frameMs) {
    static const METRIC::MetricHandle cpuMs =
        METRIC::Register({METRIC::kFrameCpuMs, kMetricOwner, "Runtime", "ms", METRIC::MetricCategory::Work,
                          "CPU time of the whole frame."});
    METRIC::Publish(cpuMs, frameMs, gState.frameIndex);
}
} // namespace

const char* ToString(Pass pass) { return IsValidPass(pass) ? kPassInfo[static_cast<size_t>(pass)].name : ""; }

void BeginFrame(uint64_t frameIndex, double previousFrameSeconds) {
    static const METRIC::MetricHandle fps =
        METRIC::Register({METRIC::kFrameFps, kMetricOwner, "Runtime", "fps", METRIC::MetricCategory::Count,
                          "Frames per second at the start of the frame."});
    if (const auto framesPerSecond = PERFORMANCE::FramesPerSecond(1u, previousFrameSeconds)) {
        METRIC::Publish(fps, *framesPerSecond, frameIndex);
    }
    gState.frameActive = true;
    gState.frameIndex = frameIndex;
    gState.frameBegin = Clock::now();
    gState.elapsedMs.fill(0.0);
    gState.selfMs.fill(0.0);
    gState.callCounts.fill(0u);
    gState.scopeDepth = 0u;
    for (uint32_t index = 0u; index < gState.dynamicPassCount; ++index) {
        gState.dynamicPasses[index].elapsedMs = 0.0;
        gState.dynamicPasses[index].selfMs = 0.0;
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
    gState.elapsedMs[framePassIndex] = Milliseconds(gState.frameBegin, frameEnd);
    gState.callCounts[framePassIndex] = 1u;

    PublishFrameMetrics(gState.elapsedMs[framePassIndex]);
    if (TRACING::IsRecording()) {
        TRACING::RecordCpu(kPassInfo[framePassIndex].name, kPassInfo[framePassIndex].module, gState.frameBegin,
                           frameEnd, gState.frameIndex);
    }
    for (size_t index = 0; index < gState.passMetrics.size(); ++index) {
        const Pass pass = static_cast<Pass>(index);
        if (pass != Pass::Frame) {
            PublishSegment(gState.passMetrics[index], kPassInfo[index].name, kPassInfo[index].module, CategoryOf(pass),
                           gState.elapsedMs[index], gState.selfMs[index], gState.callCounts[index]);
        }
    }
    for (uint32_t index = 0u; index < gState.dynamicPassCount; ++index) {
        DynamicPassState& state = gState.dynamicPasses[index];
        PublishSegment(state.metrics, state.name, state.module, METRIC::MetricCategory::Work, state.elapsedMs,
                       state.selfMs, state.callCount);
    }
    // 診断（環境変数 HIKARI_SPIKE_TRACE=閾値ms、既定 40）：CPU の 1 フレームか、前のフレームの終わりからの間隔が
    // 閾値を超えたら、そのフレームで長かった計測を多い順に出す（突然の引っかかりの原因を探す）。
    static const double spikeThreshold = [] {
        const char* value = std::getenv("HIKARI_SPIKE_TRACE");
        return value == nullptr ? -1.0 : (std::atof(value) > 0.0 ? std::atof(value) : 40.0);
    }();
    static Clock::time_point previousEnd{};
    const double intervalMs = previousEnd == Clock::time_point{} ? 0.0 : Milliseconds(previousEnd, frameEnd);
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
        std::fprintf(stderr, "[Spike] frame=%llu cpu=%.1fms interval=%.1fms",
                     static_cast<unsigned long long>(gState.frameIndex), cpuMs, intervalMs);
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
    gState.dynamicPasses[handle].module = ModuleOfSegment(name);
    return handle;
}

ScopedCpuTimer::ScopedCpuTimer(Pass pass)
    : pass_(pass), begin_(Clock::now()), active_(gState.frameActive && IsValidPass(pass)) {
    if (active_) {
        depth_ = EnterScope();
    }
}

ScopedCpuTimer::~ScopedCpuTimer() {
    if (active_) {
        Record(pass_, begin_, Clock::now(), depth_);
    }
}

ScopedDynamicCpuTimer::ScopedDynamicCpuTimer(DynamicPassHandle pass)
    : pass_(pass), begin_(Clock::now()), active_(gState.frameActive && pass < gState.dynamicPassCount) {
    if (active_) {
        depth_ = EnterScope();
    }
}

ScopedDynamicCpuTimer::~ScopedDynamicCpuTimer() {
    if (active_) {
        RecordDynamic(pass_, begin_, Clock::now(), depth_);
    }
}

} // namespace HIKARI::CPU_PROFILE
