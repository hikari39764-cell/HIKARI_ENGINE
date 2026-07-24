#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Upscaling/HIKARI_StreamlineInternal.h"
#include "Render3D/Upscaling/HIKARI_StreamlineResourceAdapter.h"

#if defined(HIKARI_WITH_STREAMLINE)
#pragma warning(push, 0)
#include <sl_dlss_g.h>
#pragma warning(pop)
#endif

namespace HIKARI::RENDER3D::UPSCALING {

    namespace {
        void SetStatus(
            StreamlineFrameGenerationStats& stats,
            StreamlineFrameGenerationStatus status,
            std::string reason = {}) {
            stats.status = status;
            stats.statusReason = std::move(reason);
        }
    }

#if defined(HIKARI_WITH_STREAMLINE)
    namespace {
        constexpr uint64_t kFrameGenerationRetryBaseFrames = 300u;
        constexpr uint64_t kFrameGenerationRetryMaxFrames = 1200u;

        void OnFrameGenerationApiError(const sl::APIError& error) {
            if (error.hres == DXGI_STATUS_OCCLUDED) {
                return;
            }
            std::ostringstream message;
            message << "[Streamline] DLSS-G DXGI callback result=0x"
                << std::hex << std::uppercase << std::setw(8)
                << std::setfill('0')
                << static_cast<uint32_t>(error.hres);
            if (FAILED(error.hres)) {
                HIKARI_LOG_ERROR(message.str());
            } else {
                HIKARI_LOG_WARN(message.str());
            }
        }

        sl::Resource MakeResource(RenderTarget2D& target) {
            sl::Resource resource(
                sl::ResourceType::eTex2d,
                target.GetResource(),
                static_cast<uint32_t>(target.GetColorState()));
            resource.width = static_cast<uint32_t>(target.GetWidth());
            resource.height = static_cast<uint32_t>(target.GetHeight());
            resource.nativeFormat = static_cast<uint32_t>(target.GetFormat());
            resource.mipLevels = 1;
            resource.arrayLayers = 1;
            return resource;
        }

        bool SameSettings(
            const StreamlineFrameGenerationSettings& left,
            const StreamlineFrameGenerationSettings& right) {
            return
                left.enabled == right.enabled &&
                left.generatedFrames == right.generatedFrames &&
                left.retainResourcesWhenOff == right.retainResourcesWhenOff &&
                left.enableUiRecomposition == right.enableUiRecomposition;
        }

        bool FailureSignatureChanged(
            const INTERNAL::StreamlineFrameGenerationRuntimeState& state,
            const TEMPORAL::TemporalInputs& temporal,
            const RenderTarget2D& hudless) {
            return
                !SameSettings(state.settings, state.failedSettings) ||
                state.failedRenderWidth != temporal.frame.renderWidth ||
                state.failedRenderHeight != temporal.frame.renderHeight ||
                state.failedOutputWidth !=
                    static_cast<uint32_t>(hudless.GetWidth()) ||
                state.failedOutputHeight !=
                    static_cast<uint32_t>(hudless.GetHeight());
        }

        void ClearFailure(
            INTERNAL::StreamlineFrameGenerationRuntimeState& state) {
            state.lastFailureResult = sl::Result::eOk;
            state.stats.retryFrameIndex = 0;
            state.stats.consecutiveFailureCount = 0;
            state.stats.retryPending = false;
        }

        void RecordFailure(
            INTERNAL::StreamlineState& state,
            const char* operation,
            sl::Result result,
            const TEMPORAL::TemporalInputs& temporal,
            const RenderTarget2D& hudless) {
            INTERNAL::StreamlineFrameGenerationRuntimeState& frameGeneration =
                state.frameGeneration;
            (void)INTERNAL::RecordResult(state, operation, result);
            ++frameGeneration.stats.failureCount;
            frameGeneration.lastFailureResult = result;
            frameGeneration.failedSettings = frameGeneration.settings;
            frameGeneration.failedRenderWidth = temporal.frame.renderWidth;
            frameGeneration.failedRenderHeight = temporal.frame.renderHeight;
            frameGeneration.failedOutputWidth =
                static_cast<uint32_t>(hudless.GetWidth());
            frameGeneration.failedOutputHeight =
                static_cast<uint32_t>(hudless.GetHeight());
            frameGeneration.stats.consecutiveFailureCount = (std::min)(
                frameGeneration.stats.consecutiveFailureCount + 1u,
                4u);
            const uint64_t retryDelay = (std::min)(
                kFrameGenerationRetryBaseFrames <<
                    (frameGeneration.stats.consecutiveFailureCount - 1u),
                kFrameGenerationRetryMaxFrames);
            frameGeneration.stats.retryFrameIndex =
                temporal.frame.frameIndex + retryDelay;
            frameGeneration.stats.retryPending = true;
            SetStatus(
                frameGeneration.stats,
                result == sl::Result::eWarnOutOfVRAM
                    ? StreamlineFrameGenerationStatus::ResourcePressure
                    : StreamlineFrameGenerationStatus::RuntimeFailure,
                std::string(operation) + ": " + state.stats.lastResult);
        }

        bool NeedsConfiguration(
            const INTERNAL::StreamlineFrameGenerationRuntimeState& state,
            const TEMPORAL::TemporalInputs& temporal,
            const RenderTarget2D& hudless,
            const RenderTarget2D& ui) {
            const uint32_t supportedFrames =
                state.stats.stateValid && state.stats.maxGeneratedFrames > 0
                ? state.stats.maxGeneratedFrames
                : 1u;
            const uint32_t requestedGeneratedFrames = std::clamp(
                state.settings.generatedFrames,
                1u,
                supportedFrames);
            return
                !state.stats.optionsConfigured ||
                !SameSettings(state.settings, state.configuredSettings) ||
                state.configuredGeneratedFrames != requestedGeneratedFrames ||
                state.configuredRenderWidth != temporal.frame.renderWidth ||
                state.configuredRenderHeight != temporal.frame.renderHeight ||
                state.configuredOutputWidth !=
                    static_cast<uint32_t>(hudless.GetWidth()) ||
                state.configuredOutputHeight !=
                    static_cast<uint32_t>(hudless.GetHeight()) ||
                state.configuredDepthFormat != temporal.sceneDepth.format ||
                state.configuredMotionFormat != temporal.motionVectors.format ||
                state.configuredHudlessFormat != hudless.GetFormat() ||
                state.configuredUiFormat != ui.GetFormat();
        }

        sl::DLSSGOptions BuildOptions(
            const INTERNAL::StreamlineFrameGenerationRuntimeState& state,
            const TEMPORAL::TemporalInputs& temporal,
            const RenderTarget2D& hudless,
            const RenderTarget2D& ui) {
            sl::DLSSGOptions options{};
            options.mode = state.settings.enabled
                ? sl::DLSSGMode::eOn
                : sl::DLSSGMode::eOff;
            const uint32_t supportedFrames =
                state.stats.stateValid && state.stats.maxGeneratedFrames > 0
                ? state.stats.maxGeneratedFrames
                : 1u;
            options.numFramesToGenerate = std::clamp(
                state.settings.generatedFrames,
                1u,
                supportedFrames);
            if (state.settings.retainResourcesWhenOff) {
                options.flags |= sl::DLSSGFlags::eRetainResourcesWhenOff;
            }
            options.flags |= sl::DLSSGFlags::eEnableFullscreenMenuDetection;
            options.numBackBuffers = GFX::kFrameResourceCount;
            options.mvecDepthWidth = temporal.frame.renderWidth;
            options.mvecDepthHeight = temporal.frame.renderHeight;
            options.colorWidth = static_cast<uint32_t>(hudless.GetWidth());
            options.colorHeight = static_cast<uint32_t>(hudless.GetHeight());
            options.colorBufferFormat =
                static_cast<uint32_t>(DXGI_FORMAT_R8G8B8A8_UNORM);
            options.mvecBufferFormat =
                static_cast<uint32_t>(temporal.motionVectors.format);
            options.depthBufferFormat =
                static_cast<uint32_t>(temporal.sceneDepth.format);
            options.hudLessBufferFormat =
                static_cast<uint32_t>(hudless.GetFormat());
            options.uiBufferFormat = static_cast<uint32_t>(ui.GetFormat());
            options.onErrorCallback = OnFrameGenerationApiError;
            options.queueParallelismMode =
                sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
            options.enableUserInterfaceRecomposition =
                state.settings.enableUiRecomposition
                ? sl::Boolean::eTrue
                : sl::Boolean::eFalse;
            return options;
        }

        bool ConfigureOptions(
            INTERNAL::StreamlineState& state,
            const TEMPORAL::TemporalInputs& temporal,
            RenderTarget2D& hudless,
            RenderTarget2D& ui) {
            INTERNAL::StreamlineFrameGenerationRuntimeState& frameGeneration =
                state.frameGeneration;
            const sl::DLSSGOptions options = BuildOptions(
                frameGeneration,
                temporal,
                hudless,
                ui);
            const sl::Result result = slDLSSGSetOptions(state.viewport, options);
            if (result != sl::Result::eOk) {
                RecordFailure(
                    state,
                    "slDLSSGSetOptions",
                    result,
                    temporal,
                    hudless);
                frameGeneration.stats.optionsConfigured = false;
                return false;
            }

            ClearFailure(frameGeneration);
            frameGeneration.configuredSettings = frameGeneration.settings;
            frameGeneration.configuredRenderWidth = temporal.frame.renderWidth;
            frameGeneration.configuredRenderHeight = temporal.frame.renderHeight;
            frameGeneration.configuredOutputWidth =
                static_cast<uint32_t>(hudless.GetWidth());
            frameGeneration.configuredOutputHeight =
                static_cast<uint32_t>(hudless.GetHeight());
            frameGeneration.configuredDepthFormat = temporal.sceneDepth.format;
            frameGeneration.configuredMotionFormat = temporal.motionVectors.format;
            frameGeneration.configuredHudlessFormat = hudless.GetFormat();
            frameGeneration.configuredUiFormat = ui.GetFormat();
            frameGeneration.configuredGeneratedFrames =
                options.numFramesToGenerate;
            frameGeneration.stats.optionsConfigured = true;
            frameGeneration.resourcesReleased = false;
            frameGeneration.completionStateCapturedAfterPresent = false;
            SetStatus(
                frameGeneration.stats,
                frameGeneration.settings.enabled
                    ? StreamlineFrameGenerationStatus::Active
                    : StreamlineFrameGenerationStatus::Disabled);
            HIKARI_LOG_INFO(
                std::string("[Streamline] DLSS-G configured mode=") +
                (frameGeneration.settings.enabled ? "on" : "off") +
                " multiplier=" +
                std::to_string(frameGeneration.settings.generatedFrames + 1u) +
                " render=" + std::to_string(temporal.frame.renderWidth) + "x" +
                std::to_string(temporal.frame.renderHeight) +
                " output=" + std::to_string(hudless.GetWidth()) + "x" +
                std::to_string(hudless.GetHeight()));
            return true;
        }

    }
#endif

    void SetStreamlineFrameGenerationSettings(
        const StreamlineFrameGenerationSettings& settings) {
        INTERNAL::StreamlineFrameGenerationRuntimeState& state =
            INTERNAL::GetState().frameGeneration;
        const bool changed =
            state.settings.enabled != settings.enabled ||
            state.settings.generatedFrames != settings.generatedFrames ||
            state.settings.retainResourcesWhenOff !=
                settings.retainResourcesWhenOff ||
            state.settings.enableUiRecomposition !=
                settings.enableUiRecomposition;
        state.settings = settings;
        state.settings.generatedFrames =
            (std::max)(1u, state.settings.generatedFrames);
        state.stats.requested = state.settings.enabled;
        state.stats.generatedFrames = state.settings.generatedFrames;
        if (changed) {
            state.stats.retryFrameIndex = 0;
            state.stats.consecutiveFailureCount = 0;
            state.stats.retryPending = false;
#if defined(HIKARI_WITH_STREAMLINE)
            state.lastFailureResult = sl::Result::eOk;
#endif
        }
        if (!state.settings.enabled) {
            SetStatus(state.stats, StreamlineFrameGenerationStatus::Disabled);
        }
    }

    const StreamlineFrameGenerationSettings&
        GetStreamlineFrameGenerationSettings() {
        return INTERNAL::GetState().frameGeneration.settings;
    }

    const char* ToString(StreamlineFrameGenerationStatus status) {
        switch (status) {
        case StreamlineFrameGenerationStatus::Disabled: return "disabled";
        case StreamlineFrameGenerationStatus::Unavailable: return "unavailable";
        case StreamlineFrameGenerationStatus::HostDisallowed: return "host disallowed";
        case StreamlineFrameGenerationStatus::WaitingForInputs: return "waiting for inputs";
        case StreamlineFrameGenerationStatus::Configuring: return "configuring";
        case StreamlineFrameGenerationStatus::Active: return "active";
        case StreamlineFrameGenerationStatus::Suspended: return "suspended";
        case StreamlineFrameGenerationStatus::BlockedByUpscaler: return "upscaler blocked";
        case StreamlineFrameGenerationStatus::ResourcePressure: return "resource pressure";
        case StreamlineFrameGenerationStatus::SdkRejectedInputs: return "SDK rejected inputs";
        case StreamlineFrameGenerationStatus::RuntimeFailure: return "runtime failure";
        default: return "unknown";
        }
    }

    bool SubmitStreamlineFrameGenerationInputs(
        const TEMPORAL::TemporalInputs& temporal,
        RenderTarget2D& hudlessColor,
        RenderTarget2D& uiColorAndAlpha,
        uint64_t frameIndex,
        bool hostAllowed) {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
        INTERNAL::StreamlineFrameGenerationRuntimeState& frameGeneration =
            state.frameGeneration;
        StreamlineFrameGenerationStats& stats = frameGeneration.stats;
        stats.frameIndex = frameIndex;
        stats.hostAllowed = hostAllowed;
        stats.requested = frameGeneration.settings.enabled;
        stats.generatedFrames = frameGeneration.settings.generatedFrames;
        stats.backBufferTagged = false;
        stats.tagsSubmitted = false;

        const bool presentationMatches =
            hudlessColor.GetResource() != nullptr &&
            uiColorAndAlpha.GetResource() != nullptr &&
            hudlessColor.GetWidth() == uiColorAndAlpha.GetWidth() &&
            hudlessColor.GetHeight() == uiColorAndAlpha.GetHeight() &&
            hudlessColor.GetWidth() == state.context.backBufferWidth &&
            hudlessColor.GetHeight() == state.context.backBufferHeight;
        stats.inputsReady =
            temporal.frame.frameIndex == frameIndex &&
            temporal.sceneDepth.valid &&
            temporal.motionVectors.valid &&
            presentationMatches;

#if !defined(HIKARI_WITH_STREAMLINE)
        SetStatus(stats, StreamlineFrameGenerationStatus::Unavailable);
        return false;
#else
        if (!hostAllowed) {
            SetStatus(stats, StreamlineFrameGenerationStatus::HostDisallowed);
            return false;
        }
        if (!stats.featureLoaded) {
            SetStatus(
                stats,
                StreamlineFrameGenerationStatus::Unavailable,
                "DLSS-G feature is not loaded for this presentation target");
            return false;
        }
        if (!stats.supported) {
            SetStatus(stats, StreamlineFrameGenerationStatus::Unavailable);
            return false;
        }
        if (
            state.frameToken == nullptr ||
            state.stats.frameIndex != frameIndex ||
            !presentationMatches) {
            SetStatus(
                stats,
                StreamlineFrameGenerationStatus::WaitingForInputs,
                "presentation contract incomplete");
            return false;
        }

        const sl::Extent outputExtent{
            0u,
            0u,
            static_cast<uint32_t>(hudlessColor.GetWidth()),
            static_cast<uint32_t>(hudlessColor.GetHeight()) };
        const sl::ResourceTag backBufferTag(
            nullptr,
            sl::kBufferTypeBackbuffer,
            sl::ResourceLifecycle{},
            &outputExtent);
        const sl::Result backBufferResult = slSetTagForFrame(
            *state.frameToken,
            state.viewport,
            &backBufferTag,
            1u,
            reinterpret_cast<sl::CommandBuffer*>(state.context.cmdList));
        if (backBufferResult != sl::Result::eOk) {
            RecordFailure(
                state,
                "slSetTagForFrame(DLSS-G backbuffer)",
                backBufferResult,
                temporal,
                hudlessColor);
            return false;
        }
        stats.backBufferTagged = true;

        if (!state.stats.constantsSubmitted || !stats.inputsReady) {
            SetStatus(
                stats,
                StreamlineFrameGenerationStatus::WaitingForInputs,
                "temporal inputs incomplete");
            return false;
        }

        if (frameGeneration.settings.enabled &&
            state.stats.dlssRequested &&
            !state.stats.dlssEvaluated) {
            const bool releaseFrameGenerationResources =
                state.stats.status ==
                    StreamlineRuntimeStatus::ResourcePressure;
            DeactivateStreamlineFrameGeneration(
                releaseFrameGenerationResources);
            SetStatus(
                stats,
                StreamlineFrameGenerationStatus::BlockedByUpscaler,
                state.stats.lastResult.empty()
                    ? "DLSS did not produce this frame"
                    : state.stats.lastResult);
            return false;
        }

        if (frameGeneration.settings.enabled &&
            (!state.reflex.stats.optionsConfigured ||
                state.reflex.stats.mode == StreamlineReflexMode::Off)) {
            if (!ConfigureStreamlineReflex(
                    StreamlineReflexMode::LowLatency)) {
                ++stats.failureCount;
                return false;
            }
        }

        if (stats.stateValid && stats.statusFlags != 0u) {
            return false;
        }

        if (stats.retryPending &&
            FailureSignatureChanged(
                frameGeneration,
                temporal,
                hudlessColor)) {
            ClearFailure(frameGeneration);
        }
        if (stats.retryPending && frameIndex < stats.retryFrameIndex) {
            return false;
        }
        if (stats.retryPending) {
            stats.retryPending = false;
        }

        if (NeedsConfiguration(
                frameGeneration,
                temporal,
                hudlessColor,
                uiColorAndAlpha)) {
            SetStatus(stats, StreamlineFrameGenerationStatus::Configuring);
            if (!ConfigureOptions(
                    state,
                    temporal,
                    hudlessColor,
                    uiColorAndAlpha)) {
                return false;
            }
        }

        if (!frameGeneration.settings.enabled) {
            SetStatus(stats, StreamlineFrameGenerationStatus::Disabled);
            return true;
        }

        sl::Resource depth =
            INTERNAL::MakeStreamlineTextureResource(
                temporal.sceneDepth);
        sl::Resource motion =
            INTERNAL::MakeStreamlineTextureResource(
                temporal.motionVectors);
        sl::Resource hudless = MakeResource(hudlessColor);
        sl::Resource ui = MakeResource(uiColorAndAlpha);
        const sl::Extent renderExtent{
            0u,
            0u,
            temporal.frame.renderWidth,
            temporal.frame.renderHeight };
        constexpr sl::ResourceLifecycle lifecycle =
            sl::ResourceLifecycle::eValidUntilPresent;
        const std::array<sl::ResourceTag, 4> tags = {
            sl::ResourceTag(
                &depth,
                sl::kBufferTypeDepth,
                lifecycle,
                &renderExtent),
            sl::ResourceTag(
                &motion,
                sl::kBufferTypeMotionVectors,
                lifecycle,
                &renderExtent),
            sl::ResourceTag(
                &hudless,
                sl::kBufferTypeHUDLessColor,
                lifecycle,
                &outputExtent),
            sl::ResourceTag(
                &ui,
                sl::kBufferTypeUIColorAndAlpha,
                lifecycle,
                &outputExtent),
        };
        const sl::Result tagResult = slSetTagForFrame(
            *state.frameToken,
            state.viewport,
            tags.data(),
            static_cast<uint32_t>(tags.size()),
            reinterpret_cast<sl::CommandBuffer*>(state.context.cmdList));
        if (tagResult != sl::Result::eOk) {
            RecordFailure(
                state,
                "slSetTagForFrame(DLSS-G)",
                tagResult,
                temporal,
                hudlessColor);
            return false;
        }
        stats.tagsSubmitted = true;
        frameGeneration.completionStateCapturedAfterPresent = false;
        SetStatus(stats, StreamlineFrameGenerationStatus::Active);
        return true;
#endif
    }

    bool IsStreamlineFrameGenerationInstalled() {
        return INTERNAL::GetState().frameGeneration.stats.pluginPresent;
    }

    bool IsStreamlineFrameGenerationAvailable() {
        const StreamlineFrameGenerationStats& stats =
            INTERNAL::GetState().frameGeneration.stats;
        return stats.pluginPresent && stats.supported;
    }

    bool DeactivateStreamlineFrameGeneration(bool releaseResources) {
#if defined(HIKARI_WITH_STREAMLINE)
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
        INTERNAL::StreamlineFrameGenerationRuntimeState& frameGeneration =
            state.frameGeneration;
        if (!frameGeneration.stats.supported ||
            !frameGeneration.stats.featureLoaded) {
            return true;
        }

        bool success = true;
        if (releaseResources &&
            !frameGeneration.resourcesReleased &&
            !frameGeneration.completionStateCapturedAfterPresent &&
            !CaptureStreamlineFrameGenerationCompletionAfterPresent()) {
            HIKARI_LOG_ERROR(
                "[Streamline] DLSS-G resource release blocked because the last Present completion state could not be captured.");
            return false;
        }
        if (frameGeneration.stats.optionsConfigured) {
            sl::DLSSGOptions options{};
            options.mode = sl::DLSSGMode::eOff;
            if (!releaseResources &&
                frameGeneration.settings.retainResourcesWhenOff) {
                options.flags |= sl::DLSSGFlags::eRetainResourcesWhenOff;
            }
            if (!INTERNAL::RecordResult(
                    state,
                    "slDLSSGSetOptions(suspend)",
                    slDLSSGSetOptions(state.viewport, options),
                    false)) {
                ++frameGeneration.stats.failureCount;
                success = false;
            } else {
                frameGeneration.stats.optionsConfigured = false;
                frameGeneration.stats.tagsSubmitted = false;
            }
        }
        if (releaseResources && !frameGeneration.resourcesReleased) {
            if (!WaitForStreamlineFrameGenerationInputs()) {
                HIKARI_LOG_ERROR(
                    "[Streamline] DLSS-G resource release blocked because input processing did not finish.");
                return false;
            }
            if (!INTERNAL::RecordResult(
                state,
                "slFreeResources(DLSS-G)",
                slFreeResources(sl::kFeatureDLSS_G, state.viewport),
                false)) {
                ++frameGeneration.stats.failureCount;
                success = false;
            } else {
                frameGeneration.resourcesReleased = true;
                frameGeneration.inputsProcessingCompletionFence = nullptr;
                frameGeneration.inputsProcessingCompletionFenceValue = 0;
                frameGeneration.completionStateCapturedAfterPresent = false;
            }
        }
        if (!success) {
            return false;
        }
        SetStatus(
            frameGeneration.stats,
            StreamlineFrameGenerationStatus::Suspended);
        return true;
#else
        (void)releaseResources;
        return true;
#endif
    }

    const StreamlineFrameGenerationStats&
        GetStreamlineFrameGenerationStats() {
        return INTERNAL::GetState().frameGeneration.stats;
    }

} // namespace HIKARI::RENDER3D::UPSCALING
