#include "Diagnostics/HIKARI_CpuFrameProfiler.h"

namespace HIKARI::CPU_PROFILE {

    namespace {
        using Clock = std::chrono::steady_clock;

        struct State {
            bool frameActive = false;
            uint64_t frameIndex = 0;
            Clock::time_point frameBegin{};
            std::array<double, static_cast<size_t>(Pass::Count)> elapsedMs{};
            std::array<uint32_t, static_cast<size_t>(Pass::Count)> callCounts{};
            FrameSnapshot latest{};
        };

        State gState{};

        bool IsValidPass(Pass pass) {
            return static_cast<size_t>(pass) < static_cast<size_t>(Pass::Count);
        }

        void Record(Pass pass, Clock::duration elapsed) {
            if (!gState.frameActive || !IsValidPass(pass) || pass == Pass::Frame) {
                return;
            }
            const size_t index = static_cast<size_t>(pass);
            gState.elapsedMs[index] +=
                std::chrono::duration<double, std::milli>(elapsed).count();
            ++gState.callCounts[index];
        }
    }

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
        case Pass::PostResolve: return "Post Resolve";
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
    }

    void EndFrame() {
        if (!gState.frameActive) {
            return;
        }

        const Clock::time_point frameEnd = Clock::now();
        const size_t framePassIndex = static_cast<size_t>(Pass::Frame);
        gState.elapsedMs[framePassIndex] =
            std::chrono::duration<double, std::milli>(frameEnd - gState.frameBegin).count();
        gState.callCounts[framePassIndex] = 1u;

        FrameSnapshot snapshot{};
        snapshot.valid = true;
        snapshot.frameIndex = gState.frameIndex;
        for (size_t index = 0; index < snapshot.passes.size(); ++index) {
            PassTiming& timing = snapshot.passes[index];
            timing.name = ToString(static_cast<Pass>(index));
            timing.callCount = gState.callCounts[index];
            timing.valid = timing.callCount != 0u;
            timing.cpuMs = gState.elapsedMs[index];
        }
        gState.latest = snapshot;
        gState.frameActive = false;
    }

    const FrameSnapshot& GetLatestSnapshot() {
        return gState.latest;
    }

    ScopedCpuTimer::ScopedCpuTimer(Pass pass)
        : pass_(pass), begin_(Clock::now()), active_(gState.frameActive && IsValidPass(pass)) {
    }

    ScopedCpuTimer::~ScopedCpuTimer() {
        if (active_) {
            Record(pass_, Clock::now() - begin_);
        }
    }

} // namespace HIKARI::CPU_PROFILE
