#include "Diagnostics/HIKARI_CpuFrameProfiler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include "Core/HIKARI_Logger.h"

namespace HIKARI::CPU_PROFILE {

namespace {
using Clock = std::chrono::steady_clock;

using RollingWindow = PERFORMANCE::RollingTimingWindow;

struct DynamicPassState {
    std::string name{};
    double elapsedMs = 0.0;
    uint32_t callCount = 0u;
    RollingWindow rolling{};
};

struct State {
    bool frameActive = false;
    bool discardActiveFrame = false;
    bool dynamicPassOverflowReported = false;
    uint64_t frameIndex = 0;
    Clock::time_point frameBegin{};
    std::array<double, static_cast<size_t>(Pass::Count)> elapsedMs{};
    std::array<uint32_t, static_cast<size_t>(Pass::Count)> callCounts{};
    std::array<RollingWindow, static_cast<size_t>(Pass::Count)> rolling{};
    std::array<DynamicPassState, kMaximumDynamicPasses> dynamicPasses{};
    uint32_t dynamicPassCount = 0u;
    FrameSnapshot latest{};
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

void FillTiming(PassTiming& target, const char* name, double elapsedMs, uint32_t callCount,
                const RollingWindow& rolling) {
    target.name = name;
    target.callCount = callCount;
    target.valid = callCount != 0u;
    target.cpuMs = elapsedMs;
    target.averageMs = rolling.Average();
    target.p95Ms = rolling.p95;
    target.p99Ms = rolling.p99;
    target.maximumMs = rolling.maximum;
    target.rollingSampleCount = static_cast<uint32_t>(rolling.count);
}

void ResetSnapshot() {
    gState.latest = {};
    for (size_t index = 0; index < gState.latest.passes.size(); ++index) {
        gState.latest.passes[index].name = ToString(static_cast<Pass>(index));
    }
    gState.latest.dynamicPassCount = gState.dynamicPassCount;
    for (uint32_t index = 0; index < gState.dynamicPassCount; ++index) {
        gState.latest.dynamicPasses[index].name = gState.dynamicPasses[index].name.c_str();
    }
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

void BeginFrame(uint64_t frameIndex) {
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

    gState.latest.valid = true;
    gState.latest.frameIndex = gState.frameIndex;
    for (size_t index = 0; index < gState.latest.passes.size(); ++index) {
        gState.rolling[index].Push(gState.elapsedMs[index]);
        FillTiming(gState.latest.passes[index], ToString(static_cast<Pass>(index)), gState.elapsedMs[index],
                   gState.callCounts[index], gState.rolling[index]);
    }
    gState.latest.dynamicPassCount = gState.dynamicPassCount;
    for (uint32_t index = 0u; index < gState.dynamicPassCount; ++index) {
        DynamicPassState& state = gState.dynamicPasses[index];
        state.rolling.Push(state.elapsedMs);
        FillTiming(gState.latest.dynamicPasses[index], state.name.c_str(), state.elapsedMs, state.callCount,
                   state.rolling);
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
    gState.rolling = {};
    for (uint32_t index = 0u; index < gState.dynamicPassCount; ++index) {
        gState.dynamicPasses[index].rolling = {};
    }
    gState.discardActiveFrame = gState.frameActive;
    ResetSnapshot();
}

const FrameSnapshot& GetLatestSnapshot() { return gState.latest; }

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
