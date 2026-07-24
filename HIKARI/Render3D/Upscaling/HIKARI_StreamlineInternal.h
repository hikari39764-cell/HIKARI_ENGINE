#pragma once

#include <string>

#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"
#include "Render3D/Upscaling/HIKARI_StreamlineReflex.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"

#if defined(HIKARI_WITH_STREAMLINE)
#pragma warning(push, 0)
#include <sl.h>
#pragma warning(pop)
#endif

namespace HIKARI::RENDER3D::UPSCALING::INTERNAL {

    inline constexpr const char* kStreamlineSdkVersion = "2.12.0";

    struct StreamlinePluginState {
        bool dlss = false;
        bool frameGeneration = false;
        bool reflex = false;
        bool pcl = false;
    };

    struct StreamlineReflexRuntimeState {
        StreamlineReflexStats stats{};
    };

    struct StreamlineFrameGenerationRuntimeState {
        StreamlineFrameGenerationSettings settings{};
        StreamlineFrameGenerationSettings configuredSettings{};
        StreamlineFrameGenerationStats stats{};
        uint32_t configuredRenderWidth = 0;
        uint32_t configuredRenderHeight = 0;
        uint32_t configuredOutputWidth = 0;
        uint32_t configuredOutputHeight = 0;
        DXGI_FORMAT configuredDepthFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT configuredMotionFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT configuredHudlessFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT configuredUiFormat = DXGI_FORMAT_UNKNOWN;
        uint32_t configuredGeneratedFrames = 0;
        StreamlineFrameGenerationSettings failedSettings{};
        uint32_t failedRenderWidth = 0;
        uint32_t failedRenderHeight = 0;
        uint32_t failedOutputWidth = 0;
        uint32_t failedOutputHeight = 0;
        bool resourcesReleased = true;
        void* inputsProcessingCompletionFence = nullptr;
        uint64_t inputsProcessingCompletionFenceValue = 0;
        bool completionStateCapturedAfterPresent = false;
#if defined(HIKARI_WITH_STREAMLINE)
        sl::Result lastFailureResult = sl::Result::eOk;
#endif
    };

    struct StreamlineState {
        GFX::Context context{};
        RenderTarget2D output{};
        DXGI_FORMAT outputFormat = DXGI_FORMAT_UNKNOWN;
        uint32_t outputWidth = 0;
        uint32_t outputHeight = 0;
        StreamlineDebugStats stats{};
        StreamlinePluginState plugins{};
        StreamlineReflexRuntimeState reflex{};
        StreamlineFrameGenerationRuntimeState frameGeneration{};
#if defined(HIKARI_WITH_STREAMLINE)
        sl::FrameToken* frameToken = nullptr;
        sl::ViewportHandle viewport{ 0u };
        bool optionsConfigured = false;
        StreamlineDlssMode configuredMode = StreamlineDlssMode::Off;
        uint32_t configuredOutputWidth = 0;
        uint32_t configuredOutputHeight = 0;
        bool optimalSettingsCached = false;
        StreamlineDlssMode optimalSettingsMode = StreamlineDlssMode::Off;
        uint32_t optimalSettingsOutputWidth = 0;
        uint32_t optimalSettingsOutputHeight = 0;
        StreamlineOptimalSettings optimalSettings{};
        std::wstring pluginPath{};
        std::wstring logPath{};
        sl::Result dlssLastFailureResult = sl::Result::eOk;
        StreamlineDlssMode dlssFailedMode = StreamlineDlssMode::Off;
        uint32_t dlssFailedRenderWidth = 0;
        uint32_t dlssFailedRenderHeight = 0;
        uint32_t dlssFailedOutputWidth = 0;
        uint32_t dlssFailedOutputHeight = 0;
#endif
    };

    StreamlineState& GetState();
    void ResetFrameStats(StreamlineState& state, uint64_t frameIndex);

#if defined(HIKARI_WITH_STREAMLINE)
    bool RecordResult(
        StreamlineState& state,
        const char* operation,
        sl::Result result,
        bool countFailure = true);
#endif

} // namespace HIKARI::RENDER3D::UPSCALING::INTERNAL
