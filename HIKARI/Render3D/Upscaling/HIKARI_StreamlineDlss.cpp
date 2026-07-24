#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"

#include <algorithm>
#include <array>
#include <iterator>

#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Upscaling/HIKARI_StreamlineInternal.h"
#include "Render3D/Upscaling/HIKARI_StreamlineResourceAdapter.h"

#if defined(HIKARI_WITH_STREAMLINE)
#pragma warning(push, 0)
#include <sl_dlss.h>
#include <sl_helpers.h>
#pragma warning(pop)
#endif

namespace HIKARI::RENDER3D::UPSCALING {

#if defined(HIKARI_WITH_STREAMLINE)
    namespace {
        constexpr uint64_t kDlssRetryBaseFrames = 300u;
        constexpr uint64_t kDlssRetryMaxFrames = 1200u;

        sl::DLSSMode ToSlDlssMode(StreamlineDlssMode mode) {
            switch (mode) {
            case StreamlineDlssMode::Dlaa: return sl::DLSSMode::eDLAA;
            case StreamlineDlssMode::Quality: return sl::DLSSMode::eMaxQuality;
            case StreamlineDlssMode::Balanced: return sl::DLSSMode::eBalanced;
            case StreamlineDlssMode::Performance: return sl::DLSSMode::eMaxPerformance;
            case StreamlineDlssMode::UltraPerformance:
                return sl::DLSSMode::eUltraPerformance;
            case StreamlineDlssMode::Off:
            default:
                return sl::DLSSMode::eOff;
            }
        }

        sl::DLSSOptions BuildDlssOptions(
            StreamlineDlssMode mode,
            uint32_t outputWidth,
            uint32_t outputHeight) {
            sl::DLSSOptions options{};
            options.mode = ToSlDlssMode(mode);
            options.outputWidth = outputWidth;
            options.outputHeight = outputHeight;
            options.preExposure = 1.0f;
            options.exposureScale = 1.0f;
            options.colorBuffersHDR = sl::Boolean::eTrue;
            options.useAutoExposure = sl::Boolean::eFalse;
            options.alphaUpscalingEnabled = sl::Boolean::eFalse;
            options.dlaaPreset = sl::DLSSPreset::ePresetK;
            options.qualityPreset = sl::DLSSPreset::ePresetK;
            options.balancedPreset = sl::DLSSPreset::ePresetK;
            options.performancePreset = sl::DLSSPreset::ePresetM;
            options.ultraPerformancePreset = sl::DLSSPreset::ePresetL;
            return options;
        }

        bool EnsureDlssOutput(
            INTERNAL::StreamlineState& state,
            uint32_t width,
            uint32_t height,
            DXGI_FORMAT format) {
            if (state.context.device == nullptr || state.context.cmdList == nullptr) {
                return false;
            }

            const uint32_t safeWidth = (std::max)(1u, width);
            const uint32_t safeHeight = (std::max)(1u, height);
            state.output.UpdateContext(state.context);
            const bool recreate =
                state.output.GetResource() == nullptr ||
                state.outputWidth != safeWidth ||
                state.outputHeight != safeHeight ||
                state.outputFormat != format;
            if (recreate) {
                state.output.Finalize();
                state.output.UpdateContext(state.context);
                state.output.SetDebugName("Streamline.DLSS.Output");
                if (!state.output.Init(
                        static_cast<int>(safeWidth),
                        static_cast<int>(safeHeight),
                        format,
                        false,
                        { 0.0f, 0.0f, 0.0f, 0.0f },
                        false,
                        true)) {
                    state.stats.outputReady = false;
                    return false;
                }
                state.outputWidth = safeWidth;
                state.outputHeight = safeHeight;
                state.outputFormat = format;
                state.optionsConfigured = false;
            }
            state.stats.outputReady = state.output.GetResource() != nullptr;
            return state.stats.outputReady;
        }

        bool DlssFailureSignatureChanged(
            const INTERNAL::StreamlineState& state,
            const TEMPORAL::TemporalInputs& inputs,
            StreamlineDlssMode mode) {
            return
                state.dlssFailedMode != mode ||
                state.dlssFailedRenderWidth != inputs.frame.renderWidth ||
                state.dlssFailedRenderHeight != inputs.frame.renderHeight ||
                state.dlssFailedOutputWidth != inputs.frame.outputWidth ||
                state.dlssFailedOutputHeight != inputs.frame.outputHeight;
        }

        void ClearDlssFailure(INTERNAL::StreamlineState& state) {
            state.dlssLastFailureResult = sl::Result::eOk;
            state.stats.retryFrameIndex = 0;
            state.stats.consecutiveFailureCount = 0;
            state.stats.retryPending = false;
        }

        void RecordDlssFailure(
            INTERNAL::StreamlineState& state,
            const char* operation,
            sl::Result result,
            const TEMPORAL::TemporalInputs& inputs,
            StreamlineDlssMode mode) {
            (void)INTERNAL::RecordResult(state, operation, result);
            state.dlssLastFailureResult = result;
            state.dlssFailedMode = mode;
            state.dlssFailedRenderWidth = inputs.frame.renderWidth;
            state.dlssFailedRenderHeight = inputs.frame.renderHeight;
            state.dlssFailedOutputWidth = inputs.frame.outputWidth;
            state.dlssFailedOutputHeight = inputs.frame.outputHeight;
            state.stats.consecutiveFailureCount =
                (std::min)(state.stats.consecutiveFailureCount + 1u, 4u);
            const uint64_t retryDelay = (std::min)(
                kDlssRetryBaseFrames <<
                    (state.stats.consecutiveFailureCount - 1u),
                kDlssRetryMaxFrames);
            state.stats.retryFrameIndex = inputs.frame.frameIndex + retryDelay;
            state.stats.retryPending = true;
            state.stats.status = result == sl::Result::eWarnOutOfVRAM
                ? StreamlineRuntimeStatus::ResourcePressure
                : StreamlineRuntimeStatus::RuntimeFailure;
        }
    }
#endif

    const char* ToString(StreamlineDlssMode mode) {
        switch (mode) {
        case StreamlineDlssMode::Off: return "off";
        case StreamlineDlssMode::Dlaa: return "DLAA";
        case StreamlineDlssMode::Quality: return "DLSS Quality";
        case StreamlineDlssMode::Balanced: return "DLSS Balanced";
        case StreamlineDlssMode::Performance: return "DLSS Performance";
        case StreamlineDlssMode::UltraPerformance: return "DLSS Ultra Performance";
        default: return "unknown";
        }
    }

    StreamlineDlssMode ResolveStreamlineDlssMode(
        const RenderQualitySettings& settings) {
        if (IsDlaaAntiAliasingMode(settings.antiAliasingMode)) {
            return StreamlineDlssMode::Dlaa;
        }
        if (!IsDlssAntiAliasingMode(settings.antiAliasingMode)) {
            return StreamlineDlssMode::Off;
        }
        switch (settings.dlssQualityMode) {
        case DlssQualityMode::Quality: return StreamlineDlssMode::Quality;
        case DlssQualityMode::Balanced: return StreamlineDlssMode::Balanced;
        case DlssQualityMode::Performance: return StreamlineDlssMode::Performance;
        case DlssQualityMode::UltraPerformance:
            return StreamlineDlssMode::UltraPerformance;
        default:
            return StreamlineDlssMode::Quality;
        }
    }

    bool IsStreamlineDlssSuperResolutionMode(StreamlineDlssMode mode) {
        return mode == StreamlineDlssMode::Quality ||
            mode == StreamlineDlssMode::Balanced ||
            mode == StreamlineDlssMode::Performance ||
            mode == StreamlineDlssMode::UltraPerformance;
    }

    bool QueryStreamlineDlssOptimalSettings(
        StreamlineDlssMode mode,
        uint32_t outputWidth,
        uint32_t outputHeight,
        StreamlineOptimalSettings& outSettings) {
        outSettings = {};
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
#if !defined(HIKARI_WITH_STREAMLINE)
        (void)mode;
        (void)outputWidth;
        (void)outputHeight;
        return false;
#else
        if (mode == StreamlineDlssMode::Off ||
            !IsStreamlineDlssAvailable() ||
            outputWidth == 0 ||
            outputHeight == 0) {
            return false;
        }
        if (state.optimalSettingsCached &&
            state.optimalSettingsMode == mode &&
            state.optimalSettingsOutputWidth == outputWidth &&
            state.optimalSettingsOutputHeight == outputHeight) {
            outSettings = state.optimalSettings;
            return outSettings.valid;
        }

        const sl::DLSSOptions options =
            BuildDlssOptions(mode, outputWidth, outputHeight);
        sl::DLSSOptimalSettings optimal{};
        if (!INTERNAL::RecordResult(
                state,
                "slDLSSGetOptimalSettings",
                slDLSSGetOptimalSettings(options, optimal),
                false)) {
            return false;
        }

        outSettings.valid =
            optimal.optimalRenderWidth > 0 && optimal.optimalRenderHeight > 0;
        outSettings.optimalRenderWidth = optimal.optimalRenderWidth;
        outSettings.optimalRenderHeight = optimal.optimalRenderHeight;
        outSettings.minRenderWidth = optimal.renderWidthMin;
        outSettings.minRenderHeight = optimal.renderHeightMin;
        outSettings.maxRenderWidth = optimal.renderWidthMax;
        outSettings.maxRenderHeight = optimal.renderHeightMax;

        state.optimalSettingsCached = true;
        state.optimalSettingsMode = mode;
        state.optimalSettingsOutputWidth = outputWidth;
        state.optimalSettingsOutputHeight = outputHeight;
        state.optimalSettings = outSettings;
        state.stats.optimalRenderWidth = outSettings.optimalRenderWidth;
        state.stats.optimalRenderHeight = outSettings.optimalRenderHeight;
        state.stats.minRenderWidth = outSettings.minRenderWidth;
        state.stats.minRenderHeight = outSettings.minRenderHeight;
        state.stats.maxRenderWidth = outSettings.maxRenderWidth;
        state.stats.maxRenderHeight = outSettings.maxRenderHeight;
        state.stats.lastOperation = "slDLSSGetOptimalSettings";
        state.stats.lastResult = "eOk";
        return outSettings.valid;
#endif
    }

    RenderTarget2D* ExecuteStreamlineDlss(
        const TEMPORAL::TemporalInputs& inputs,
        StreamlineDlssMode mode) {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
        state.stats.dlssRequested = mode != StreamlineDlssMode::Off;
        state.stats.mode = mode;
#if !defined(HIKARI_WITH_STREAMLINE)
        (void)inputs;
        (void)mode;
        return nullptr;
#else
        if (state.stats.retryPending &&
            DlssFailureSignatureChanged(state, inputs, mode)) {
            ClearDlssFailure(state);
        }
        if (state.stats.retryPending &&
            inputs.frame.frameIndex < state.stats.retryFrameIndex) {
            return nullptr;
        }
        if (state.stats.retryPending) {
            state.stats.retryPending = false;
        }

        const bool requiredInputsReady =
            inputs.sceneColor.valid &&
            inputs.sceneDepth.valid &&
            inputs.motionVectors.valid &&
            inputs.exposure.valid &&
            inputs.reactiveMask.valid &&
            inputs.transparencyMask.valid &&
            inputs.invalidDepthMotionMask.valid;
        StreamlineOptimalSettings optimal{};
        const bool optimalSettingsReady = QueryStreamlineDlssOptimalSettings(
            mode,
            inputs.frame.outputWidth,
            inputs.frame.outputHeight,
            optimal);
        const bool renderSizeValid =
            mode == StreamlineDlssMode::Dlaa
                ? inputs.frame.renderWidth == inputs.frame.outputWidth &&
                    inputs.frame.renderHeight == inputs.frame.outputHeight
                : IsStreamlineDlssSuperResolutionMode(mode) &&
                    optimalSettingsReady &&
                    inputs.frame.renderWidth >= optimal.minRenderWidth &&
                    inputs.frame.renderHeight >= optimal.minRenderHeight &&
                    inputs.frame.renderWidth <= optimal.maxRenderWidth &&
                    inputs.frame.renderHeight <= optimal.maxRenderHeight;
        const bool inputExtentsValid =
            inputs.sceneColor.width == inputs.frame.renderWidth &&
            inputs.sceneColor.height == inputs.frame.renderHeight &&
            inputs.sceneDepth.width == inputs.frame.renderWidth &&
            inputs.sceneDepth.height == inputs.frame.renderHeight &&
            inputs.motionVectors.width == inputs.frame.renderWidth &&
            inputs.motionVectors.height == inputs.frame.renderHeight;
        if (!IsStreamlineDlssAvailable() ||
            !state.stats.frameTokenReady ||
            !state.stats.constantsSubmitted ||
            state.frameToken == nullptr ||
            state.context.cmdList == nullptr ||
            !requiredInputsReady ||
            inputs.frame.frameIndex != state.stats.frameIndex ||
            !renderSizeValid ||
            !inputExtentsValid) {
            state.stats.lastOperation = "validate DLSS inputs";
            state.stats.lastResult = "incomplete or mismatched temporal contract";
            ++state.stats.failureCount;
            return nullptr;
        }

        if (!EnsureDlssOutput(
                state,
                inputs.frame.outputWidth,
                inputs.frame.outputHeight,
                inputs.sceneColor.format)) {
            state.stats.lastOperation = "create DLSS output";
            state.stats.lastResult = "UAV render target creation failed";
            ++state.stats.failureCount;
            return nullptr;
        }

        if (!state.optionsConfigured ||
            state.configuredMode != mode ||
            state.configuredOutputWidth != inputs.frame.outputWidth ||
            state.configuredOutputHeight != inputs.frame.outputHeight) {
            const sl::DLSSOptions options = BuildDlssOptions(
                mode,
                inputs.frame.outputWidth,
                inputs.frame.outputHeight);
            const sl::Result optionsResult =
                slDLSSSetOptions(state.viewport, options);
            if (optionsResult != sl::Result::eOk) {
                RecordDlssFailure(
                    state,
                    "slDLSSSetOptions",
                    optionsResult,
                    inputs,
                    mode);
                return nullptr;
            }
            state.optionsConfigured = true;
            state.configuredMode = mode;
            state.configuredOutputWidth = inputs.frame.outputWidth;
            state.configuredOutputHeight = inputs.frame.outputHeight;
        }

        state.output.TransitionColor(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        TEMPORAL::TemporalTextureView outputView{};
        outputView.resource = state.output.GetResource();
        outputView.srv = state.output.GetSrvGpu();
        outputView.format = state.output.GetFormat();
        outputView.width = static_cast<uint32_t>(state.output.GetWidth());
        outputView.height = static_cast<uint32_t>(state.output.GetHeight());
        outputView.state = state.output.GetColorState();
        outputView.valid = outputView.resource != nullptr;

        sl::Resource colorInput =
            INTERNAL::MakeStreamlineTextureResource(inputs.sceneColor);
        sl::Resource colorOutput =
            INTERNAL::MakeStreamlineTextureResource(outputView);
        sl::Resource depth =
            INTERNAL::MakeStreamlineTextureResource(inputs.sceneDepth);
        sl::Resource motion =
            INTERNAL::MakeStreamlineTextureResource(inputs.motionVectors);
        sl::Resource exposure =
            INTERNAL::MakeStreamlineTextureResource(inputs.exposure);
        sl::Resource reactive =
            INTERNAL::MakeStreamlineTextureResource(inputs.reactiveMask);
        sl::Resource transparency =
            INTERNAL::MakeStreamlineTextureResource(
                inputs.transparencyMask);
        sl::Resource invalidDepthMotion =
            INTERNAL::MakeStreamlineTextureResource(
                inputs.invalidDepthMotionMask);

        const sl::Extent renderExtent{
            0u, 0u, inputs.frame.renderWidth, inputs.frame.renderHeight };
        const sl::Extent outputExtent{
            0u, 0u, inputs.frame.outputWidth, inputs.frame.outputHeight };
        const sl::Extent exposureExtent{ 0u, 0u, 1u, 1u };
        const sl::ResourceLifecycle lifecycle =
            sl::ResourceLifecycle::eValidUntilEvaluate;
        const std::array<sl::ResourceTag, 8> tags = {
            sl::ResourceTag(&colorInput, sl::kBufferTypeScalingInputColor, lifecycle, &renderExtent),
            sl::ResourceTag(&colorOutput, sl::kBufferTypeScalingOutputColor, lifecycle, &outputExtent),
            sl::ResourceTag(&depth, sl::kBufferTypeDepth, lifecycle, &renderExtent),
            sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors, lifecycle, &renderExtent),
            sl::ResourceTag(&exposure, sl::kBufferTypeExposure, lifecycle, &exposureExtent),
            sl::ResourceTag(&reactive, sl::kBufferTypeReactiveMaskHint, lifecycle, &renderExtent),
            sl::ResourceTag(&transparency, sl::kBufferTypeTransparencyAndCompositionMaskHint, lifecycle, &renderExtent),
            sl::ResourceTag(&invalidDepthMotion, sl::kBufferTypeInvalidDepthMotionHint, lifecycle, &renderExtent),
        };

        ID3D12GraphicsCommandList* cmd = state.context.cmdList;
        GFX::PIX::ScopedGpuEvent pix(
            cmd,
            GFX::PIX::kColorRender,
            mode == StreamlineDlssMode::Dlaa
                ? "Temporal.Streamline.DLAA"
                : "Temporal.Streamline.DLSS");
        GFX::GPU_PROFILE::ScopedGpuTimer timer(
            cmd,
            mode == StreamlineDlssMode::Dlaa
                ? GFX::GPU_PROFILE::Pass::TemporalDlaaResolve
                : GFX::GPU_PROFILE::Pass::TemporalDlssResolve);

        const sl::Result tagResult = slSetTagForFrame(
            *state.frameToken,
            state.viewport,
            tags.data(),
            static_cast<uint32_t>(tags.size()),
            reinterpret_cast<sl::CommandBuffer*>(cmd));
        if (tagResult != sl::Result::eOk) {
            RecordDlssFailure(
                state,
                "slSetTagForFrame(DLSS)",
                tagResult,
                inputs,
                mode);
            state.output.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            return nullptr;
        }

        const sl::BaseStructure* evaluateInputs[] = { &state.viewport };
        const sl::Result evaluateResult = slEvaluateFeature(
            sl::kFeatureDLSS,
            *state.frameToken,
            evaluateInputs,
            static_cast<uint32_t>(std::size(evaluateInputs)),
            reinterpret_cast<sl::CommandBuffer*>(cmd));
        state.output.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (evaluateResult != sl::Result::eOk) {
            RecordDlssFailure(
                state,
                "slEvaluateFeature(DLSS)",
                evaluateResult,
                inputs,
                mode);
            GFX::DumpD3D12InfoQueue(
                state.context.device,
                "Streamline DLSS evaluate failed");
            return nullptr;
        }

        ClearDlssFailure(state);
        state.stats.dlssEvaluated = true;
        ++state.stats.evaluationCount;
        state.stats.status = StreamlineRuntimeStatus::Ready;
        state.stats.lastOperation = "slEvaluateFeature(DLSS)";
        state.stats.lastResult = "eOk";

        if (state.stats.evaluationCount == 1u ||
            (state.stats.evaluationCount % 120u) == 0u) {
            sl::DLSSState dlssState{};
            if (slDLSSGetState(state.viewport, dlssState) == sl::Result::eOk) {
                state.stats.estimatedVramBytes = dlssState.estimatedVRAMUsageInBytes;
            }
        }
        return &state.output;
#endif
    }

    void MarkStreamlineDlssFallback() {
        INTERNAL::GetState().stats.fallbackUsed = true;
    }

} // namespace HIKARI::RENDER3D::UPSCALING
