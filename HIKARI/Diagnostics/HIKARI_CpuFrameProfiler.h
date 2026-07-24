#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace HIKARI::CPU_PROFILE {

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
        uint32_t callCount = 0;
    };

    struct FrameSnapshot {
        bool valid = false;
        uint64_t frameIndex = 0;
        std::array<PassTiming, static_cast<size_t>(Pass::Count)> passes{};
    };

    const char* ToString(Pass pass);
    void BeginFrame(uint64_t frameIndex);
    void EndFrame();
    const FrameSnapshot& GetLatestSnapshot();

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

} // namespace HIKARI::CPU_PROFILE
