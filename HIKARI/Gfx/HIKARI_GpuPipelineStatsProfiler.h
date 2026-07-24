#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>

#include "Gfx/HIKARI_GpuFrameProfiler.h"

namespace HIKARI::GFX::GPU_PIPELINE_STATS {

    struct PassPipelineStats {
        const char* name = "";
        bool valid = false;
        D3D12_QUERY_DATA_PIPELINE_STATISTICS1 counters{};
    };

    struct FrameSnapshot {
        bool initialized = false;
        bool profilerEnabled = true;
        bool pipelineStatsAvailable = false;
        bool meshShaderPipelineStatsSupported = false;
        uint64_t frameIndex = 0;
        const char* unavailableReason = "";
        std::array<PassPipelineStats, static_cast<size_t>(GPU_PROFILE::Pass::Count)> passes{};
    };

    void SetEnabled(bool enabled, const char* unavailableReason = "");
    bool IsEnabled();

    void BeginFrame(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmd,
        uint64_t frameIndex);
    void EndFrame(ID3D12GraphicsCommandList* cmd);
    void Shutdown();

    bool BeginPass(ID3D12GraphicsCommandList* cmd, GPU_PROFILE::Pass pass);
    void EndPass(ID3D12GraphicsCommandList* cmd, GPU_PROFILE::Pass pass);

    const FrameSnapshot& GetLatestSnapshot();

    class ScopedPipelineStats {
    public:
        ScopedPipelineStats(ID3D12GraphicsCommandList* cmd, GPU_PROFILE::Pass pass);
        ~ScopedPipelineStats();

        ScopedPipelineStats(const ScopedPipelineStats&) = delete;
        ScopedPipelineStats& operator=(const ScopedPipelineStats&) = delete;

    private:
        ID3D12GraphicsCommandList* cmd_ = nullptr;
        GPU_PROFILE::Pass pass_ = GPU_PROFILE::Pass::Count;
        bool active_ = false;
    };

} // namespace HIKARI::GFX::GPU_PIPELINE_STATS
