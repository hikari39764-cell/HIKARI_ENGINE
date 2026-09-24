#pragma once

#include "Core/HIKARI_RollingTimingWindow.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace HIKARI::CPU_PROFILE {

inline constexpr std::size_t kRollingWindowFrameCount = PERFORMANCE::kTimingWindowSampleCount;
inline constexpr std::size_t kMaximumDynamicPasses = 128u;
using DynamicPassHandle = uint32_t;
inline constexpr DynamicPassHandle kInvalidDynamicPassHandle = static_cast<DynamicPassHandle>(-1);

enum class Pass : uint32_t {
    Frame = 0,
    ServicesBeginFrame,
    AppUpdate,
    AppRender,
    AppImGui,
    RenderSubmission,
    ShadowPrepare,
    ShadowSourceSync,
    MeshPrepare,
    MaterialPrepare,
    PostResolve,
    UiLayers,
    ImGui,
    CommandSubmit,
    Present,
    FenceWait,
    Count
};

struct PassTiming {
    const char* name = "";
    bool valid = false;
    double cpuMs = 0.0;
    double averageMs = 0.0;
    double p95Ms = 0.0;
    double p99Ms = 0.0;
    double maximumMs = 0.0;
    uint32_t callCount = 0;
    uint32_t rollingSampleCount = 0;
};

struct FrameSnapshot {
    bool valid = false;
    uint64_t frameIndex = 0;
    std::array<PassTiming, static_cast<size_t>(Pass::Count)> passes{};
    std::array<PassTiming, kMaximumDynamicPasses> dynamicPasses{};
    uint32_t dynamicPassCount = 0;
};

const char* ToString(Pass pass);
void BeginFrame(uint64_t frameIndex);
void EndFrame();
const FrameSnapshot& GetLatestSnapshot();
DynamicPassHandle RegisterDynamicPass(std::string_view name);

class ScopedCpuTimer {
public:
    explicit ScopedCpuTimer(Pass pass);
    ~ScopedCpuTimer();

    ScopedCpuTimer(const ScopedCpuTimer&) = delete;
    ScopedCpuTimer& operator=(const ScopedCpuTimer&) = delete;

private:
    Pass pass_ = Pass::Count;
    std::chrono::steady_clock::time_point begin_{};
    bool active_ = false;
};

class ScopedDynamicCpuTimer {
public:
    explicit ScopedDynamicCpuTimer(DynamicPassHandle pass);
    ~ScopedDynamicCpuTimer();

    ScopedDynamicCpuTimer(const ScopedDynamicCpuTimer&) = delete;
    ScopedDynamicCpuTimer& operator=(const ScopedDynamicCpuTimer&) = delete;

private:
    DynamicPassHandle pass_ = kInvalidDynamicPassHandle;
    std::chrono::steady_clock::time_point begin_{};
    bool active_ = false;
};

} // namespace HIKARI::CPU_PROFILE
