#include "Vfx/Post/HIKARI_PostSystem.h"

#include <algorithm>
#include <sstream>

#include "Core/Presentation/HIKARI_LogicalDisplayDefaults.h"
#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Render3D/Debug/HIKARI_RenderDebugOutput.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalResolveStage.h"
#include "Vfx/Post/HIKARI_GameUiCompositionStage.h"
#include "Vfx/Post/HIKARI_PostChain.h"
#include "Vfx/Post/HIKARI_PostPresentationStage.h"
#include "Vfx/Post/HIKARI_PostProcessingStage.h"
#include "Vfx/Post/HIKARI_SceneCaptureStage.h"

namespace HIKARI::POST {

    namespace {
        constexpr int kMinViewportSize = 16;
        constexpr int kMaxViewportSize = 8192;
    }

    bool PostSystem::initialized_ = false;
    GFX::Context PostSystem::context_{};
    QuadDrawer PostSystem::quad_{};
    SceneCaptureStage PostSystem::captureStage_{};
    PostProcessingStage PostSystem::processingStage_{};
    GameUiCompositionStage PostSystem::gameUiCompositionStage_{};
    PostPresentationStage PostSystem::presentationStage_{};
    int PostSystem::requestedRenderWidth_ = 0;
    int PostSystem::requestedRenderHeight_ = 0;
    int PostSystem::requestedOutputWidth_ = 0;
    int PostSystem::requestedOutputHeight_ = 0;
    RENDER3D::TEMPORAL::TemporalViewLease PostSystem::temporalView_{};
    RENDER3D::RenderFrameToken PostSystem::temporalFrame_{};
    RENDER3D::TEMPORAL::TemporalResourceFrame PostSystem::temporalResources_{};
    RENDER3D::TEMPORAL::TemporalResourceFrame
        PostSystem::presentationTemporalResources_{};

    void PostSystem::Initialize(const GFX::Context& context) {
        UpdateContext(context);
        if (initialized_) return;
        if (!context_.device || !context_.cmdList) {
            DEBUGLOG::PushRenderError(
                "[PostSystem][ERROR] Invalid graphics context.");
            return;
        }
        if (!quad_.Init(context_)) {
            DEBUGLOG::PushRenderError(
                "[PostSystem][ERROR] QuadDrawer initialization failed.");
            GFX::DumpD3D12InfoQueue(context_.device, "PostSystem initialization");
            return;
        }
        initialized_ = true;
    }

    void PostSystem::UpdateContext(const GFX::Context& context) {
        context_ = context;
        quad_.UpdateContext(context);
        captureStage_.UpdateContext(context);
        processingStage_.UpdateContext(context);
        gameUiCompositionStage_.UpdateContext(context);
        presentationStage_.UpdateContext(context);
    }

    void PostSystem::Shutdown() {
        AbortSceneCaptureTemporalFrame(
            RENDER3D::TEMPORAL::
                TemporalHistoryResetReason::PreviousFrameIncomplete);
        if (!initialized_) return;
        captureStage_.Shutdown();
        processingStage_.Shutdown();
        gameUiCompositionStage_.Shutdown();
        presentationStage_.Shutdown();
        quad_.Finalize();
        initialized_ = false;
    }

    void PostSystem::UpdateCommonParams(float deltaTime) {
        if (!initialized_) Initialize(context_);
        int width = 0;
        int height = 0;
        GetSceneCaptureSize(width, height);
        processingStage_.UpdateCommonParams(deltaTime, width, height);
    }

    void PostSystem::SetIntensity(float value) { processingStage_.SetIntensity(value); }
    void PostSystem::SetCombo(float value) { processingStage_.SetCombo(value); }
    void PostSystem::ClearEffects() { processingStage_.ClearEffects(); }
    void PostSystem::AddEffect(PostEffect* effect) { processingStage_.AddEffect(effect); }
    bool PostSystem::SetGlobalProfile(
        const std::string& id,
        const DirectX::XMFLOAT4(&values)[16]) {
        return processingStage_.SetGlobalProfile(id, values);
    }
    void PostSystem::ClearGlobalProfile() { processingStage_.ClearGlobalProfile(); }
    void PostSystem::SetBloomSettings(const BloomSettings& settings) { processingStage_.SetBloomSettings(settings); }
    void PostSystem::SetToneMappingSettings(const ToneMappingSettings& settings) { processingStage_.SetToneMappingSettings(settings); }
    void PostSystem::SetFxaaSettings(const FxaaSettings& settings) { processingStage_.SetFxaaSettings(settings); }
    void PostSystem::SetSunGlareFrame(const SunGlareFrame& frame) { processingStage_.SetSunGlareFrame(frame); }
    void PostSystem::ClearSunGlareFrame() { processingStage_.ClearSunGlareFrame(); }
    void PostSystem::SetActionScreenImpulseFrame(
        const RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrameSource& source,
        const MATH::Mat4& viewProj,
        uint64_t frameIndex) {
        processingStage_.SetActionScreenImpulseFrame(
            source,
            viewProj,
            frameIndex);
    }
    void PostSystem::SetActionScreenDistortionMask(
        const PostSystem::ActionScreenDistortionMask& mask) {
        processingStage_.SetActionScreenDistortionMask(
            mask);
    }
    void PostSystem::SetActionScreenDistortionAccent(
        const PostSystem::ActionScreenDistortionAccent& accent) {
        processingStage_.SetActionScreenDistortionAccent(
            accent);
    }
    const PostSystem::FxaaSettings& PostSystem::GetFxaaSettings() { return processingStage_.GetFxaaSettings(); }
    const PostSystem::BloomDebugStats& PostSystem::GetBloomDebugStats() { return processingStage_.GetBloomDebugStats(); }
    const PostSystem::SunGlareDebugStats& PostSystem::GetSunGlareDebugStats() { return processingStage_.GetSunGlareDebugStats(); }
    const PostSystem::ActionScreenImpulseDebugState&
        PostSystem::GetActionScreenImpulseDebugState() {
        return processingStage_.GetActionScreenImpulseDebugState();
    }
    const RENDER3D::COLOR::ExposureFrameState&
        PostSystem::GetExposureState() {
        return processingStage_.GetExposureState();
    }
    const RENDER3D::COLOR::AutomaticExposureDebugState&
        PostSystem::GetAutomaticExposureDebugState() {
        return processingStage_.GetAutomaticExposureDebugState();
    }
    void PostSystem::SetTransitionState(const TransitionVisualState& state) { processingStage_.SetTransitionState(state); }
    void PostSystem::ClearTransitionState() { processingStage_.ClearTransitionState(); }
    void PostSystem::RequestFrameDump() { processingStage_.RequestFrameDump(); }

    std::string PostSystem::DumpFrameState() {
        const auto& quality = RENDER3D::GetRenderQualitySettings();
        std::ostringstream stream;
        stream << "[PostSystem] initialized=" << initialized_
            << " aaMode=" << RENDER3D::RenderAntiAliasingModeLabel(quality.antiAliasingMode)
            << "\n" << captureStage_.DumpState()
            << "\n" << processingStage_.DumpState()
            << "\n" << quad_.DumpState();
        return stream.str();
    }

    void PostSystem::LogFrameState(const char* reason) {
        DEBUGLOG::PushRenderError(std::string("[PostSystem][DUMP] reason=") +
            (reason ? reason : "") + "\n" + DumpFrameState());
    }

    void PostSystem::SetSceneCaptureSize(
        int renderWidth,
        int renderHeight,
        int outputWidth,
        int outputHeight) {
        int nextRenderWidth = 0;
        int nextRenderHeight = 0;
        int nextOutputWidth = 0;
        int nextOutputHeight = 0;
        if (renderWidth <= 0 || renderHeight <= 0 ||
            outputWidth <= 0 || outputHeight <= 0) {
            nextRenderWidth = 0;
            nextRenderHeight = 0;
            nextOutputWidth = 0;
            nextOutputHeight = 0;
        } else {
            nextRenderWidth = std::clamp(renderWidth, kMinViewportSize, kMaxViewportSize);
            nextRenderHeight = std::clamp(renderHeight, kMinViewportSize, kMaxViewportSize);
            nextOutputWidth = std::clamp(outputWidth, kMinViewportSize, kMaxViewportSize);
            nextOutputHeight = std::clamp(outputHeight, kMinViewportSize, kMaxViewportSize);
        }
        if (requestedRenderWidth_ == nextRenderWidth &&
            requestedRenderHeight_ == nextRenderHeight &&
            requestedOutputWidth_ == nextOutputWidth &&
            requestedOutputHeight_ == nextOutputHeight) {
            return;
        }
        requestedRenderWidth_ = nextRenderWidth;
        requestedRenderHeight_ = nextRenderHeight;
        requestedOutputWidth_ = nextOutputWidth;
        requestedOutputHeight_ = nextOutputHeight;
        presentationStage_.InvalidateEditorOutput();
    }

    void PostSystem::GetSceneCaptureSize(int& width, int& height) {
        width = requestedRenderWidth_ > 0
            ? requestedRenderWidth_
            : PRESENTATION::kDefaultLogicalWidth;
        height = requestedRenderHeight_ > 0
            ? requestedRenderHeight_
            : PRESENTATION::kDefaultLogicalHeight;
    }

    void PostSystem::GetSceneOutputSize(int& width, int& height) {
        width = requestedOutputWidth_ > 0
            ? requestedOutputWidth_
            : (requestedRenderWidth_ > 0
                ? requestedRenderWidth_
                : PRESENTATION::kDefaultLogicalWidth);
        height = requestedOutputHeight_ > 0
            ? requestedOutputHeight_
            : (requestedRenderHeight_ > 0
                ? requestedRenderHeight_
                : PRESENTATION::kDefaultLogicalHeight);
    }

    void PostSystem::BeginSceneCapture() {
        AbortSceneCaptureTemporalFrame(
            RENDER3D::TEMPORAL::
                TemporalHistoryResetReason::PreviousFrameIncomplete);
        if (!initialized_) Initialize(context_);
        int width = 0;
        int height = 0;
        GetSceneCaptureSize(width, height);
        presentationStage_.InvalidateEditorOutput();
        processingStage_.ClearSunGlareFrame();
        processingStage_.UpdateCommonParams(0.0f, width, height);
        if (!initialized_ || !captureStage_.Begin(width, height)) {
            LogFrameState("BeginSceneCapture failed");
        }
    }

    bool PostSystem::SetSceneCaptureTemporalFrame(
        RENDER3D::TEMPORAL::TemporalViewLease view,
        RENDER3D::RenderFrameToken frame,
        RENDER3D::TEMPORAL::TemporalResourceFrame resources) {

        if (!view.IsValid() ||
            !frame.IsValid() ||
            !resources.IsValid() ||
            view.viewId != frame.viewId ||
            frame.viewId != RENDER3D::kPrimaryRenderViewId ||
            !captureStage_.IsActive()) {
            return false;
        }
        if (presentationTemporalResources_.IsValid()) {
            return false;
        }
        if (!resources.view.Matches(view) ||
            !resources.publication.Matches(frame)) {
            return false;
        }
        if (temporalFrame_.IsValid()) {
            if (!temporalView_.Matches(view) ||
                !temporalFrame_.Matches(frame)) {
                return false;
            }
            if (!temporalResources_.IsValid()) {
                temporalResources_ = resources;
                return true;
            }
            return resources.IsValid() &&
                temporalResources_.Matches(resources);
        }
        temporalView_ = view;
        temporalFrame_ = frame;
        temporalResources_ = resources;
        return true;
    }

    bool PostSystem::AbortSceneCaptureTemporalFrame(
        RENDER3D::TEMPORAL::TemporalHistoryResetReason reason) {

        bool closed = true;
        if (presentationTemporalResources_.IsValid()) {
            if (RENDER3D::TEMPORAL::AbortTemporalResources(
                    presentationTemporalResources_,
                    reason)) {
                presentationTemporalResources_ = {};
            } else {
                closed = false;
            }
        }
        if (temporalResources_.IsValid()) {
            if (RENDER3D::TEMPORAL::AbortTemporalResources(
                    temporalResources_,
                    reason)) {
                temporalView_ = {};
                temporalFrame_ = {};
                temporalResources_ = {};
            } else {
                closed = false;
            }
        } else if (temporalView_.IsValid() && temporalFrame_.IsValid()) {
            bool cpuClosed = RENDER3D::TEMPORAL::AbortTemporalFrame(
                temporalView_, temporalFrame_, reason);
            if (!cpuClosed) {
                RENDER3D::TEMPORAL::TemporalFrameState state{};
                cpuClosed = RENDER3D::TEMPORAL::TryGetTemporalFrameState(
                    temporalView_, temporalFrame_, state) && state.completed;
            }
            if (cpuClosed) {
                temporalView_ = {};
                temporalFrame_ = {};
            } else {
                closed = false;
            }
        }
        return closed;
    }

    bool PostSystem::TryGetPresentationTemporalInputs(
        RENDER3D::TEMPORAL::TemporalInputs& outInputs) {

        return RENDER3D::TEMPORAL::TryGetTemporalResolveInputs(
            presentationTemporalResources_,
            outInputs);
    }

    bool PostSystem::FinishPresentationTemporalFrame(bool succeeded) {
        if (!presentationTemporalResources_.IsValid()) {
            return true;
        }
        const auto resources = presentationTemporalResources_;
        bool completed = succeeded
            ? RENDER3D::TEMPORAL::EndTemporalResources(resources)
            : RENDER3D::TEMPORAL::AbortTemporalResources(
                resources,
                RENDER3D::TEMPORAL::
                    TemporalHistoryResetReason::PreviousFrameIncomplete);
        if (!completed && succeeded) {
            completed = RENDER3D::TEMPORAL::AbortTemporalResources(
                resources,
                RENDER3D::TEMPORAL::
                    TemporalHistoryResetReason::PreviousFrameIncomplete);
        }
        if (completed) {
            presentationTemporalResources_ = {};
        }
        return completed;
    }

    bool PostSystem::IsSceneCaptureActive() { return captureStage_.IsActive(); }
    bool PostSystem::HasCurrentRenderTarget() { return initialized_ && captureStage_.HasCurrentTarget(); }
    bool PostSystem::RebindCurrentRenderTarget() { return captureStage_.RebindCurrentTarget(); }
    bool PostSystem::RebindPresentationBackBuffer() {
        if (!initialized_ || context_.cmdList == nullptr ||
            context_.rtv.ptr == 0u || context_.backBufferWidth <= 0 ||
            context_.backBufferHeight <= 0) {
            return false;
        }
        presentationStage_.BindBackBufferFullViewport();
        return true;
    }
    RENDER3D::RenderTextureView PostSystem::GetCurrentRenderTargetDepthView() {
        return captureStage_.GetCurrentDepthView();
    }
    D3D12_CPU_DESCRIPTOR_HANDLE PostSystem::GetCurrentRenderTargetDsv() { return captureStage_.GetCurrentDsv(); }
    D3D12_CPU_DESCRIPTOR_HANDLE PostSystem::GetCurrentRenderTargetReadOnlyDsv() { return captureStage_.GetCurrentReadOnlyDsv(); }
    bool PostSystem::BeginCurrentRenderTargetDepthRead() { return captureStage_.BeginCurrentDepthRead(); }
    void PostSystem::EndCurrentRenderTargetDepthRead() { captureStage_.EndCurrentDepthRead(); }

    RENDER3D::RenderTextureView PostSystem::CaptureSceneColorSnapshot() {
        return captureStage_.CaptureSceneColorSnapshot();
    }
    RENDER3D::RenderTextureView PostSystem::GetSceneColorSnapshotView() {
        return captureStage_.GetSceneColorSnapshotView();
    }

    bool PostSystem::IsEditorViewportReady() {
        return presentationStage_.IsEditorOutputReady(
            requestedOutputWidth_, requestedOutputHeight_);
    }
    D3D12_GPU_DESCRIPTOR_HANDLE PostSystem::GetEditorViewportSrv() { return presentationStage_.GetEditorOutputSrv(); }
    int PostSystem::GetEditorViewportWidth() { return presentationStage_.GetEditorOutputWidth(); }
    int PostSystem::GetEditorViewportHeight() { return presentationStage_.GetEditorOutputHeight(); }

    void PostSystem::SetAmbientColor(float r, float g, float b) { captureStage_.SetAmbientColor(r, g, b); }
    void PostSystem::BeginLightCapture() { captureStage_.BeginLightCapture(); }
    void PostSystem::EndLightCapture() { captureStage_.EndLightCapture(); }

    RenderTarget2D* PostSystem::EndSceneCaptureAndResolveFinal() {
        if (!initialized_) {
            AbortSceneCaptureTemporalFrame(
                RENDER3D::TEMPORAL::
                    TemporalHistoryResetReason::PreviousFrameIncomplete);
            return nullptr;
        }
        GFX::GPU_PROFILE::ScopedGpuTimer timer(
            context_.cmdList, GFX::GPU_PROFILE::Pass::PostResolve);
        RenderTarget2D* captured = captureStage_.End();
        if (!captured) {
            AbortSceneCaptureTemporalFrame(
                RENDER3D::TEMPORAL::
                    TemporalHistoryResetReason::PreviousFrameIncomplete);
            return nullptr;
        }

        RenderTarget2D* volumetric =
            RENDER3D::VOLUMETRIC::ExecuteVolumetricLightingStage(*captured);
        if (volumetric == nullptr || volumetric->GetResource() == nullptr) {
            volumetric = captured;
        }

        const auto& quality = RENDER3D::GetRenderQualitySettings();
        const auto handoffView = temporalView_;
        const auto handoffFrame = temporalFrame_;
        const auto handoffResources = temporalResources_;
        auto temporal = RENDER3D::TEMPORAL::ExecuteTemporalResolveStage(
            handoffView,
            handoffFrame,
            handoffResources,
            context_,
            *volumetric,
            *captured,
            quality,
            processingStage_.GetExposureState());
        RENDER3D::TEMPORAL::TemporalInputs presentationInputs{};
        bool retainHandoff = false;
        if (handoffResources.IsValid() &&
            RENDER3D::TEMPORAL::TryGetTemporalResolveInputs(
                handoffResources,
                presentationInputs)) {
            presentationTemporalResources_ = handoffResources;
        } else if (handoffResources.IsValid()) {
            retainHandoff =
                !RENDER3D::TEMPORAL::AbortTemporalResources(
                    handoffResources,
                    RENDER3D::TEMPORAL::
                        TemporalHistoryResetReason::PreviousFrameIncomplete);
        }
        if (!retainHandoff) {
            (void)RENDER3D::TEMPORAL::AbortTemporalFrame(
                handoffView,
                handoffFrame,
                RENDER3D::TEMPORAL::
                    TemporalHistoryResetReason::PreviousFrameIncomplete);
            temporalView_ = {};
            temporalFrame_ = {};
            temporalResources_ = {};
        }
        RenderTarget2D* output = temporal.output ? temporal.output : volumetric;
        if (temporal.requiresOutputNormalization) {
            RenderTarget2D* normalized = processingStage_.NormalizeTemporalOutput(
                *output,
                temporal.expectedOutputWidth,
                temporal.expectedOutputHeight,
                quad_);
            if (normalized) {
                output = normalized;
            } else {
                output = volumetric;
                RENDER3D::TEMPORAL::TemporalViewLease temporalView{};
                if (RENDER3D::TEMPORAL::AcquireTemporalView(
                        RENDER3D::kPrimaryRenderViewId,
                        temporalView)) {
                    (void)RENDER3D::TEMPORAL::ResetTemporalFrameHistory(
                        temporalView,
                        RENDER3D::TEMPORAL::
                            TemporalHistoryResetReason::ExplicitReset);
                }
            }
        }
        return processingStage_.ResolveHdr(
            *output,
            quad_,
            RENDER3D::GetRenderDebugOutputRoute().bypassPostProcessing,
            RENDER3D::TEMPORAL::ToString(temporal.backend),
            captureStage_.IsLightingEnabled(),
            captured);
    }

    RenderTarget2D* PostSystem::ResolveFinalSceneToLdr(
        RenderTarget2D& source,
        DXGI_FORMAT outputFormat) {
        if (RENDER3D::GetRenderDebugOutputRoute().bypassPostProcessing) {
            return processingStage_.ResolveDebugLdr(
                source,
                outputFormat,
                quad_);
        }
        return processingStage_.ResolveLdr(
            source,
            outputFormat,
            quad_,
            captureStage_.GetLightTarget());
    }

    bool PostSystem::PrepareGameUiComposition(
        bool editorViewport,
        int uiLogicalWidth,
        int uiLogicalHeight) {
        if (gameUiCompositionStage_.IsActive()) {
            return true;
        }
        if (!captureStage_.IsActive()) {
            return false;
        }

        RenderTarget2D* hdr = EndSceneCaptureAndResolveFinal();
        if (!hdr || !context_.cmdList) {
            presentationStage_.BindBackBufferFullViewport();
            if (editorViewport) {
                presentationStage_.InvalidateEditorOutput();
            }
            return false;
        }

        RenderTarget2D* ldr = ResolveFinalSceneToLdr(*hdr, DXGI_FORMAT_R8G8B8A8_UNORM);
        if (!ldr) {
            presentationStage_.BindBackBufferFullViewport();
            if (editorViewport) {
                presentationStage_.InvalidateEditorOutput();
            }
            return false;
        }

        int outputWidth = editorViewport
            ? requestedOutputWidth_
            : context_.backBufferWidth;
        int outputHeight = editorViewport
            ? requestedOutputHeight_
            : context_.backBufferHeight;
        if (outputWidth <= 0 || outputHeight <= 0) {
            outputWidth = ldr->GetWidth();
            outputHeight = ldr->GetHeight();
        }

        const int logicalWidth = uiLogicalWidth > 0
            ? uiLogicalWidth
            : outputWidth;
        const int logicalHeight = uiLogicalHeight > 0
            ? uiLogicalHeight
            : outputHeight;
        return gameUiCompositionStage_.Begin(
            *ldr,
            quad_,
            outputWidth,
            outputHeight,
            logicalWidth,
            logicalHeight,
            TIME::GetFrameContext().frameIndex);
    }

    bool PostSystem::FinalizeGameUiComposition(bool editorViewport) {
        if (!gameUiCompositionStage_.IsActive()) {
            return false;
        }

        RenderTarget2D* finalColor = gameUiCompositionStage_.End(quad_);
        if (finalColor == nullptr) {
            presentationStage_.BindBackBufferFullViewport();
            if (editorViewport) {
                presentationStage_.InvalidateEditorOutput();
            }
            return false;
        }

        if (editorViewport) {
            GFX::GPU_PROFILE::ScopedGpuTimer timer(
                context_.cmdList,
                GFX::GPU_PROFILE::Pass::GameViewResolve);
            return presentationStage_.PresentToEditor(
                *finalColor,
                quad_,
                requestedOutputWidth_,
                requestedOutputHeight_);
        }

        return presentationStage_.PresentToBackBuffer(*finalColor, quad_);
    }

    const PresentationFrameResources& PostSystem::GetPresentationFrameResources() {
        return gameUiCompositionStage_.GetFrameResources();
    }

    bool PostSystem::EndSceneCaptureToEditorViewport() {
        if (!captureStage_.IsActive()) {
            return IsEditorViewportReady();
        }
        return
            PrepareGameUiComposition(true) &&
            FinalizeGameUiComposition(true);
    }

    void PostSystem::EndSceneCaptureAndPresent() {
        if (!captureStage_.IsActive()) {
            return;
        }
        if (PrepareGameUiComposition(false)) {
            (void)FinalizeGameUiComposition(false);
        }
    }

    void PostSystem::BeginLayer(
        PostChain& chain, float r, float g, float b, float a) {
        if (!captureStage_.IsActive()) BeginSceneCapture();
        captureStage_.BeginLayer(chain, r, g, b, a);
    }

    void PostSystem::EndLayer(BlendOption blendMode) {
        captureStage_.EndLayer(
            blendMode, quad_, processingStage_.GetCommonParams());
    }

} // namespace HIKARI::POST
