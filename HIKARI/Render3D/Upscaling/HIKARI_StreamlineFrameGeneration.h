#pragma once

#include <cstdint>
#include <string>

#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI {
    class RenderTarget2D;
}

namespace HIKARI::RENDER3D::UPSCALING {

    enum class StreamlineFrameGenerationStatus : uint8_t {
        Disabled = 0,
        Unavailable,
        HostDisallowed,
        WaitingForInputs,
        Configuring,
        Active,
        Suspended,
        BlockedByUpscaler,
        ResourcePressure,
        SdkRejectedInputs,
        RuntimeFailure,
    };

    struct StreamlineFrameGenerationSettings {
        bool enabled = false;
        uint32_t generatedFrames = 1;
        bool retainResourcesWhenOff = true;
        bool enableUiRecomposition = true;
    };

    struct StreamlineFrameGenerationStats {
        bool pluginPresent = false;
        bool featureLoaded = false;
        bool supported = false;
        bool requested = false;
        bool hostAllowed = false;
        bool optionsConfigured = false;
        bool inputsReady = false;
        bool backBufferTagged = false;
        bool tagsSubmitted = false;
        bool stateValid = false;
        uint32_t generatedFrames = 1;
        uint32_t maxGeneratedFrames = 0;
        uint32_t presentedFrames = 0;
        uint32_t statusFlags = 0;
        uint64_t estimatedVramBytes = 0;
        uint64_t frameIndex = 0;
        uint64_t failureCount = 0;
        uint64_t retryFrameIndex = 0;
        uint32_t consecutiveFailureCount = 0;
        bool retryPending = false;
        StreamlineFrameGenerationStatus status =
            StreamlineFrameGenerationStatus::Disabled;
        std::string statusReason{};
    };

    void SetStreamlineFrameGenerationSettings(
        const StreamlineFrameGenerationSettings& settings);
    const StreamlineFrameGenerationSettings&
        GetStreamlineFrameGenerationSettings();
    bool SubmitStreamlineFrameGenerationInputs(
        const TEMPORAL::TemporalInputs& temporal,
        RenderTarget2D& hudlessColor,
        RenderTarget2D& uiColorAndAlpha,
        uint64_t frameIndex,
        bool hostAllowed);
    bool IsStreamlineFrameGenerationInstalled();
    bool IsStreamlineFrameGenerationAvailable();
    // Must run on the same thread that just presented the frame. Streamline
    // returns the fence that protects the inputs consumed by that Present.
    bool CaptureStreamlineFrameGenerationCompletionAfterPresent();
    // Waits for Streamline to finish consuming the last presented inputs.
    // Resource destruction must not proceed when this returns false.
    bool WaitForStreamlineFrameGenerationInputs(
        uint32_t timeoutMilliseconds = 5000u);
    bool DeactivateStreamlineFrameGeneration(bool releaseResources);
    const StreamlineFrameGenerationStats&
        GetStreamlineFrameGenerationStats();
    const char* ToString(StreamlineFrameGenerationStatus status);

} // namespace HIKARI::RENDER3D::UPSCALING
