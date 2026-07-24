#pragma once

namespace HIKARI::GFX {

    struct GfxDebugConfig {
        // D3D12 debug layer は検証コストが大きく、有効時は GPU frame profiler も
        // 無効化される (Dx12Core)。既定は Debug 構成のみ有効。性能計測は
        // EditorRelease 以降の構成で行い、必要なら runtime_config.json の
        // enableDebugLayer で明示的に上書きする。
#if defined(_DEBUG)
        bool enableDebugLayer = true;
#else
        bool enableDebugLayer = false;
#endif
        bool enableGpuBasedValidation = false;
        bool enableGpuFrameProfiler = true;
        bool enableGpuFrameProfilerWithDebugLayer = false;
        bool enableClusterGpuCullCounterReadback = true;
        bool enableClusterGpuCullDebugCounters = false;
        bool enableInfoQueueBreakOnError = false;
        bool enableInfoQueueBreakOnWarning = false;
        bool dumpInfoQueueOnFrameEnd = false;
        bool verboseRenderTargetLog = false;
        bool verbosePostLog = false;
    };

    const GfxDebugConfig& GetGfxDebugConfig();
    void SetGfxDebugConfig(const GfxDebugConfig& config);

}
