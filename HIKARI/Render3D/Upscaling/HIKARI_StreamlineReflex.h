#pragma once

#include <cstdint>

namespace HIKARI::RENDER3D::UPSCALING {

    enum class StreamlineReflexMode : uint8_t {
        Off = 0,
        LowLatency,
        LowLatencyWithBoost,
    };

    struct StreamlineReflexStats {
        bool reflexPluginPresent = false;
        bool pclPluginPresent = false;
        bool reflexSupported = false;
        bool pclSupported = false;
        bool optionsConfigured = false;
        bool sleepCalled = false;
        uint32_t markerMask = 0;
        uint64_t frameIndex = 0;
        uint64_t failureCount = 0;
        StreamlineReflexMode mode = StreamlineReflexMode::Off;
    };

    bool ConfigureStreamlineReflex(StreamlineReflexMode mode);
    bool BeginStreamlineReflexFrame();
    void EndStreamlineReflexSimulation();
    void MarkStreamlineReflexRenderSubmitStart();
    void MarkStreamlineReflexRenderSubmitEnd();
    void MarkStreamlineReflexPresentStart();
    void MarkStreamlineReflexPresentEnd();
    const StreamlineReflexStats& GetStreamlineReflexStats();

} // namespace HIKARI::RENDER3D::UPSCALING
