#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

namespace HIKARI::CPU_PROFILE {

// CPU の計時は登録表（Diagnostics/HIKARI_MetricRegistry.h）へ出す。
// frame.fps / frame.cpuMs / cpu.<区間名>.ms|calls|selfMs（selfMs は入れ子の子区間を除いた時間）。
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
    PresentationComposition,
    UiLayers,
    ImGui,
    CommandSubmit,
    Present,
    FenceWait,
    Count
};

const char* ToString(Pass pass);
// previousFrameSeconds は前のフレームの開始からの経過秒。測れていない時（初回など）は 0 を渡す。
void BeginFrame(uint64_t frameIndex, double previousFrameSeconds);
void EndFrame();
void ResetStatistics();
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
    uint32_t depth_ = 0;
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
    uint32_t depth_ = 0;
};

} // namespace HIKARI::CPU_PROFILE
