#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"

#include <algorithm>
#include <array>
#include <iterator>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_GpuCompletion.h"
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
    case StreamlineDlssMode::UltraPerformance: return sl::DLSSMode::eUltraPerformance;
    case StreamlineDlssMode::Off:
    default: return sl::DLSSMode::eOff;
    }
}

sl::DLSSOptions BuildDlssOptions(StreamlineDlssMode mode, uint32_t outputWidth, uint32_t outputHeight,
                                 float preExposure = 1.0f) {
    sl::DLSSOptions options{};
    options.mode = ToSlDlssMode(mode);
    options.outputWidth = outputWidth;
    options.outputHeight = outputHeight;
    options.preExposure = std::isfinite(preExposure) && preExposure > 0.0f ? preExposure : 1.0f;
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

bool EnsureDlssOutput(INTERNAL::StreamlineState& state, uint32_t width, uint32_t height, DXGI_FORMAT format) {
    if (state.context.device == nullptr || state.context.cmdList == nullptr) {
        return false;
    }

    const uint32_t safeWidth = (std::max)(1u, width);
    const uint32_t safeHeight = (std::max)(1u, height);
    state.output.UpdateContext(state.context);
    const bool recreate = state.output.GetResource() == nullptr || state.outputWidth != safeWidth ||
                          state.outputHeight != safeHeight || state.outputFormat != format;
    if (recreate) {
        state.output.Finalize();
        state.output.UpdateContext(state.context);
        state.output.SetDebugName("Streamline.DLSS.Output");
        if (!state.output.Init(static_cast<int>(safeWidth), static_cast<int>(safeHeight), format, false,
                               {0.0f, 0.0f, 0.0f, 0.0f}, false, true)) {
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

bool DlssFailureSignatureChanged(const INTERNAL::StreamlineState& state, const TEMPORAL::TemporalInputs& inputs,
                                 StreamlineDlssMode mode) {
    return state.dlssFailedMode != mode || state.dlssFailedRenderWidth != inputs.frame.renderWidth ||
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

void RecordDlssFailure(INTERNAL::StreamlineState& state, const char* operation, sl::Result result,
                       const TEMPORAL::TemporalInputs& inputs, StreamlineDlssMode mode) {
    (void)INTERNAL::RecordResult(state, operation, result);
    state.dlssLastFailureResult = result;
    state.dlssFailedMode = mode;
    state.dlssFailedRenderWidth = inputs.frame.renderWidth;
    state.dlssFailedRenderHeight = inputs.frame.renderHeight;
    state.dlssFailedOutputWidth = inputs.frame.outputWidth;
    state.dlssFailedOutputHeight = inputs.frame.outputHeight;
    state.stats.consecutiveFailureCount = (std::min)(state.stats.consecutiveFailureCount + 1u, 4u);
    const uint64_t retryDelay =
        (std::min)(kDlssRetryBaseFrames << (state.stats.consecutiveFailureCount - 1u), kDlssRetryMaxFrames);
    state.stats.retryFrameIndex = inputs.frame.frameIndex + retryDelay;
    state.stats.retryPending = true;
    state.stats.status = result == sl::Result::eWarnOutOfVRAM ? StreamlineRuntimeStatus::ResourcePressure
                                                              : StreamlineRuntimeStatus::RuntimeFailure;
}

// 実例を最後に評価したフレームの GPU 完了を待つ。そのフレームの Signal がまだ積まれていない（提出に失敗した等）時は
// 待てないので false を返し、次のフレームで再試行させる。
bool WaitForDlssInstanceRetirement(const INTERNAL::StreamlineState& state) {
    const GFX::Context& context = state.context;
    const uint64_t lastUseFenceValue = state.dlssInstanceRetireFenceValue;
    if (context.device == nullptr || context.frameFence == nullptr ||
        lastUseFenceValue >= context.currentFrameRetireFenceValue) {
        return false;
    }
    if (context.frameFence->GetCompletedValue() >= lastUseFenceValue) {
        return true;
    }
    const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const bool completed = GFX::WaitForGpuCompletion(context.device, context.frameFence, lastUseFenceValue, event,
                                                     "Streamline DLSS instance retirement");
    if (event != nullptr) {
        CloseHandle(event);
    }
    return completed;
}
} // namespace

namespace INTERNAL {
sl::Result ReleaseDlssInstance(StreamlineState& state) {
    if (state.dlssInstanceRetireFenceValue == 0u) {
        return sl::Result::eOk;
    }
    sl::Result result = slFreeResources(sl::kFeatureDLSS, state.viewport);
    // 評価が sl.dlss の begin event に届く前に失敗していれば実例は作られておらず、未登録 viewport として返る。
    if (result == sl::Result::eErrorInvalidParameter) {
        result = sl::Result::eOk;
    }
    if (result == sl::Result::eOk) {
        state.dlssInstanceRetireFenceValue = 0u;
    }
    return result;
}
} // namespace INTERNAL
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

StreamlineDlssMode ResolveStreamlineDlssMode(const RenderQualitySettings& settings) {
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
    case DlssQualityMode::UltraPerformance: return StreamlineDlssMode::UltraPerformance;
    default: return StreamlineDlssMode::Quality;
    }
}

bool IsStreamlineDlssSuperResolutionMode(StreamlineDlssMode mode) {
    return mode == StreamlineDlssMode::Quality || mode == StreamlineDlssMode::Balanced ||
           mode == StreamlineDlssMode::Performance || mode == StreamlineDlssMode::UltraPerformance;
}

bool QueryStreamlineDlssOptimalSettings(StreamlineDlssMode mode, uint32_t outputWidth, uint32_t outputHeight,
                                        StreamlineOptimalSettings& outSettings) {
    outSettings = {};
    INTERNAL::StreamlineState& state = INTERNAL::GetState();
#if !defined(HIKARI_WITH_STREAMLINE)
    (void)mode;
    (void)outputWidth;
    (void)outputHeight;
    return false;
#else
    if (mode == StreamlineDlssMode::Off || !IsStreamlineDlssAvailable() || outputWidth == 0 || outputHeight == 0) {
        return false;
    }
    if (state.optimalSettingsCached && state.optimalSettingsMode == mode &&
        state.optimalSettingsOutputWidth == outputWidth && state.optimalSettingsOutputHeight == outputHeight) {
        outSettings = state.optimalSettings;
        return outSettings.valid;
    }

    const sl::DLSSOptions options = BuildDlssOptions(mode, outputWidth, outputHeight);
    sl::DLSSOptimalSettings optimal{};
    if (!INTERNAL::RecordResult(state, "slDLSSGetOptimalSettings", slDLSSGetOptimalSettings(options, optimal), false)) {
        return false;
    }

    outSettings.valid = optimal.optimalRenderWidth > 0 && optimal.optimalRenderHeight > 0;
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

RenderTarget2D* ExecuteStreamlineDlss(const TEMPORAL::TemporalInputs& inputs, StreamlineDlssMode mode) {
    INTERNAL::StreamlineState& state = INTERNAL::GetState();
    state.stats.dlssRequested = mode != StreamlineDlssMode::Off;
    state.stats.mode = mode;
#if !defined(HIKARI_WITH_STREAMLINE)
    (void)inputs;
    (void)mode;
    return nullptr;
#else
    if (state.stats.retryPending && DlssFailureSignatureChanged(state, inputs, mode)) {
        ClearDlssFailure(state);
    }
    if (state.stats.retryPending && inputs.frame.frameIndex < state.stats.retryFrameIndex) {
        return nullptr;
    }
    if (state.stats.retryPending) {
        state.stats.retryPending = false;
    }

    const bool requiredInputsReady =
        inputs.resourceFrame.IsValid() && inputs.resourceFrame.Matches(state.temporalResources) &&
        inputs.resourceFrame.view.viewId == kPrimaryRenderViewId &&
        inputs.resourceFrame.publication.Matches(inputs.frame.publication) && inputs.sceneColor.valid &&
        inputs.sceneDepth.valid && inputs.motionVectors.valid && inputs.exposure.valid && inputs.reactiveMask.valid &&
        inputs.transparencyMask.valid && inputs.invalidDepthMotionMask.valid && inputs.biasCurrentColorMask.valid;
    StreamlineOptimalSettings optimal{};
    const bool optimalSettingsReady =
        QueryStreamlineDlssOptimalSettings(mode, inputs.frame.outputWidth, inputs.frame.outputHeight, optimal);
    const bool renderSizeValid = mode == StreamlineDlssMode::Dlaa
                                     ? inputs.frame.renderWidth == inputs.frame.outputWidth &&
                                           inputs.frame.renderHeight == inputs.frame.outputHeight
                                     : IsStreamlineDlssSuperResolutionMode(mode) && optimalSettingsReady &&
                                           inputs.frame.renderWidth >= optimal.minRenderWidth &&
                                           inputs.frame.renderHeight >= optimal.minRenderHeight &&
                                           inputs.frame.renderWidth <= optimal.maxRenderWidth &&
                                           inputs.frame.renderHeight <= optimal.maxRenderHeight;
    const bool inputExtentsValid =
        inputs.sceneColor.width == inputs.frame.renderWidth && inputs.sceneColor.height == inputs.frame.renderHeight &&
        inputs.sceneDepth.width == inputs.frame.renderWidth && inputs.sceneDepth.height == inputs.frame.renderHeight &&
        inputs.motionVectors.width == inputs.frame.renderWidth &&
        inputs.motionVectors.height == inputs.frame.renderHeight;
    if (!IsStreamlineDlssAvailable() || !state.stats.frameTokenReady || !state.stats.constantsSubmitted ||
        state.frameToken == nullptr || state.context.cmdList == nullptr || !requiredInputsReady ||
        inputs.frame.frameIndex != state.stats.frameIndex || !renderSizeValid || !inputExtentsValid) {
        state.stats.lastOperation = "validate DLSS inputs";
        state.stats.lastResult = "incomplete or mismatched temporal contract";
        ++state.stats.failureCount;
        return nullptr;
    }

    if (!EnsureDlssOutput(state, inputs.frame.outputWidth, inputs.frame.outputHeight, inputs.sceneColor.format)) {
        state.stats.lastOperation = "create DLSS output";
        state.stats.lastResult = "UAV render target creation failed";
        ++state.stats.failureCount;
        return nullptr;
    }

    // sl.dlss は mode か出力サイズが変わると旧 NGX feature の解放を 3 フレーム後の遅延ラムダとして sl.common に積む。
    // そのキューは Present フックでしか進まず、残ったまま slShutdown すると sl.dlss.dll を外した後でラムダが呼ばれて
    // 落ちる。変更前に GPU 完了を待って自分で解放し、sl.dlss には常に新規作成させる。
    const bool instanceOptionsChanged = state.configuredMode != mode ||
                                        state.configuredOutputWidth != inputs.frame.outputWidth ||
                                        state.configuredOutputHeight != inputs.frame.outputHeight;
    if (instanceOptionsChanged && state.dlssInstanceRetireFenceValue != 0u) {
        if (!WaitForDlssInstanceRetirement(state)) {
            state.stats.lastOperation = "retire DLSS instance";
            state.stats.lastResult = "previous instance GPU completion is not provable";
            ++state.stats.failureCount;
            return nullptr;
        }
        const sl::Result releaseResult = INTERNAL::ReleaseDlssInstance(state);
        if (releaseResult != sl::Result::eOk) {
            RecordDlssFailure(state, "slFreeResources(DLSS)", releaseResult, inputs, mode);
            return nullptr;
        }
    }

    if (!state.optionsConfigured || instanceOptionsChanged ||
        state.configuredPreExposure != inputs.exposureState.preExposure) {
        const sl::DLSSOptions options = BuildDlssOptions(mode, inputs.frame.outputWidth, inputs.frame.outputHeight,
                                                         inputs.exposureState.preExposure);
        const sl::Result optionsResult = slDLSSSetOptions(state.viewport, options);
        if (optionsResult != sl::Result::eOk) {
            RecordDlssFailure(state, "slDLSSSetOptions", optionsResult, inputs, mode);
            return nullptr;
        }
        state.optionsConfigured = true;
        state.configuredMode = mode;
        state.configuredPreExposure = inputs.exposureState.preExposure;
        state.configuredOutputWidth = inputs.frame.outputWidth;
        state.configuredOutputHeight = inputs.frame.outputHeight;
    }

    state.output.TransitionColor(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    RENDER3D::RenderTextureView outputView{};
    outputView.resource = state.output.GetResource();
    outputView.srv = state.output.GetSrvGpu();
    outputView.format = state.output.GetFormat();
    outputView.width = static_cast<uint32_t>(state.output.GetWidth());
    outputView.height = static_cast<uint32_t>(state.output.GetHeight());
    outputView.mipCount = 1;
    outputView.state = state.output.GetColorState();
    outputView.valid = outputView.resource != nullptr;

    sl::Resource colorInput = INTERNAL::MakeStreamlineTextureResource(inputs.sceneColor);
    sl::Resource colorOutput = INTERNAL::MakeStreamlineTextureResource(outputView);
    sl::Resource depth = INTERNAL::MakeStreamlineTextureResource(inputs.sceneDepth);
    sl::Resource motion = INTERNAL::MakeStreamlineTextureResource(inputs.motionVectors);
    sl::Resource exposure = INTERNAL::MakeStreamlineTextureResource(inputs.exposure);
    sl::Resource reactive = INTERNAL::MakeStreamlineTextureResource(inputs.reactiveMask);
    sl::Resource transparency = INTERNAL::MakeStreamlineTextureResource(inputs.transparencyMask);
    sl::Resource invalidDepthMotion = INTERNAL::MakeStreamlineTextureResource(inputs.invalidDepthMotionMask);
    sl::Resource biasCurrentColor = INTERNAL::MakeStreamlineTextureResource(inputs.biasCurrentColorMask);

    const sl::Extent renderExtent{0u, 0u, inputs.frame.renderWidth, inputs.frame.renderHeight};
    const sl::Extent outputExtent{0u, 0u, inputs.frame.outputWidth, inputs.frame.outputHeight};
    const sl::Extent exposureExtent{0u, 0u, 1u, 1u};
    const sl::ResourceLifecycle lifecycle = sl::ResourceLifecycle::eValidUntilEvaluate;
    // DLSS が履歴の扱いに読むのは BiasCurrentColor だけ（Reactive / TransparencyAndComposition は DirectSR 向けの印）。
    const std::array<sl::ResourceTag, 9> tags = {
        sl::ResourceTag(&colorInput, sl::kBufferTypeScalingInputColor, lifecycle, &renderExtent),
        sl::ResourceTag(&colorOutput, sl::kBufferTypeScalingOutputColor, lifecycle, &outputExtent),
        sl::ResourceTag(&depth, sl::kBufferTypeDepth, lifecycle, &renderExtent),
        sl::ResourceTag(&motion, sl::kBufferTypeMotionVectors, lifecycle, &renderExtent),
        sl::ResourceTag(&exposure, sl::kBufferTypeExposure, lifecycle, &exposureExtent),
        sl::ResourceTag(&reactive, sl::kBufferTypeReactiveMaskHint, lifecycle, &renderExtent),
        sl::ResourceTag(&transparency, sl::kBufferTypeTransparencyAndCompositionMaskHint, lifecycle, &renderExtent),
        sl::ResourceTag(&invalidDepthMotion, sl::kBufferTypeInvalidDepthMotionHint, lifecycle, &renderExtent),
        sl::ResourceTag(&biasCurrentColor, sl::kBufferTypeBiasCurrentColorHint, lifecycle, &renderExtent),
    };

    ID3D12GraphicsCommandList* cmd = state.context.cmdList;
    GFX::PIX::ScopedGpuEvent pix(cmd, GFX::PIX::kColorRender,
                                 mode == StreamlineDlssMode::Dlaa ? "Temporal.Streamline.DLAA"
                                                                  : "Temporal.Streamline.DLSS");
    GFX::GPU_PROFILE::ScopedGpuTimer timer(cmd, mode == StreamlineDlssMode::Dlaa
                                                    ? GFX::GPU_PROFILE::Pass::TemporalDlaaResolve
                                                    : GFX::GPU_PROFILE::Pass::TemporalDlssResolve);

    const sl::Result tagResult =
        slSetTagForFrame(*state.frameToken, state.viewport, tags.data(), static_cast<uint32_t>(tags.size()),
                         reinterpret_cast<sl::CommandBuffer*>(cmd));
    if (tagResult != sl::Result::eOk) {
        RecordDlssFailure(state, "slSetTagForFrame(DLSS)", tagResult, inputs, mode);
        state.output.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        return nullptr;
    }

    const sl::BaseStructure* evaluateInputs[] = {&state.viewport};
    const sl::Result evaluateResult =
        slEvaluateFeature(sl::kFeatureDLSS, *state.frameToken, evaluateInputs,
                          static_cast<uint32_t>(std::size(evaluateInputs)), reinterpret_cast<sl::CommandBuffer*>(cmd));
    // 評価が失敗しても begin event で実例が作られている場合があるため、結果に関わらず記録する。
    state.dlssInstanceRetireFenceValue = state.context.currentFrameRetireFenceValue;
    state.output.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    // eWarnOutOfVRAM は顕存の予算を超えたという警告で、評価は済んでいて出力は使える。失敗として TAA へ落とすと
    // 再試行の待ち（数百フレーム）ごとに DLSS と TAA が入れ替わり、履歴が切れて画面が大きく揺れる。
    // 状態だけ「資源の圧迫」にして DLSS を使い続ける。
    const bool overBudget = evaluateResult == sl::Result::eWarnOutOfVRAM;
    if (evaluateResult != sl::Result::eOk && !overBudget) {
        RecordDlssFailure(state, "slEvaluateFeature(DLSS)", evaluateResult, inputs, mode);
        GFX::DumpD3D12InfoQueue(state.context.device, "Streamline DLSS evaluate failed");
        return nullptr;
    }

    if (overBudget && !state.dlssOverBudgetReported) {
        HIKARI_LOG_WARN("[Streamline] DLSS evaluated over the VRAM budget (eWarnOutOfVRAM); keeping the DLSS output");
    }
    state.dlssOverBudgetReported = overBudget;
    ClearDlssFailure(state);
    state.stats.dlssEvaluated = true;
    ++state.stats.evaluationCount;
    state.stats.status = overBudget ? StreamlineRuntimeStatus::ResourcePressure : StreamlineRuntimeStatus::Ready;
    state.stats.lastOperation = "slEvaluateFeature(DLSS)";
    state.stats.lastResult = overBudget ? "eWarnOutOfVRAM" : "eOk";

    if (state.stats.evaluationCount == 1u || (state.stats.evaluationCount % 120u) == 0u) {
        sl::DLSSState dlssState{};
        if (slDLSSGetState(state.viewport, dlssState) == sl::Result::eOk) {
            state.stats.estimatedVramBytes = dlssState.estimatedVRAMUsageInBytes;
        }
    }
    return &state.output;
#endif
}

void MarkStreamlineDlssFallback() { INTERNAL::GetState().stats.fallbackUsed = true; }

} // namespace HIKARI::RENDER3D::UPSCALING
