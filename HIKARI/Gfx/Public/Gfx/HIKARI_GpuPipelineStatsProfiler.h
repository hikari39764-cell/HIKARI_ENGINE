#pragma once

#include <cstdint>

#include <d3d12.h>

#include "Gfx/HIKARI_GpuFrameProfiler.h"

namespace HIKARI::GFX::GPU_PIPELINE_STATS {

// 管線統計は登録表（Diagnostics/HIKARI_MetricRegistry.h）へ出す。
// gpu.pipeline.executions（1 フレームの実行数）と gpu.<pass 名>.pipeline.<項>。同じ pass の実行は合計する。

void SetEnabled(bool enabled, const char* unavailableReason = "");
bool IsEnabled();

void BeginFrame(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, uint64_t frameIndex, uint64_t completedFence,
                uint64_t retireFence);
void EndFrame(ID3D12GraphicsCommandList* cmd);
void Shutdown();
void ResetStatistics();

bool BeginPass(ID3D12GraphicsCommandList* cmd, GPU_PROFILE::Pass pass);
void EndPass(ID3D12GraphicsCommandList* cmd, GPU_PROFILE::Pass pass);

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
