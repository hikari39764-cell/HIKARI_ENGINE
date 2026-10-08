#include "Vfx/Post/HIKARI_PostSystem.h"

#include <algorithm>
#include <sstream>

#include "Core/Presentation/HIKARI_LogicalDisplayDefaults.h"
#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Render3D/DepthOfField/HIKARI_CameraDepthOfFieldStage.h"
#include "Render3D/Debug/HIKARI_RenderDebugOutput.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalResolveStage.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"
#include "Vfx/Post/HIKARI_GameUiCompositionStage.h"
#include "Vfx/Post/HIKARI_PostPresentationStage.h"
#include "Vfx/Post/HIKARI_PostProcessingStage.h"
#include "Vfx/Post/HIKARI_SceneCaptureStage.h"
#include "Vfx/Post/HIKARI_PreservedVfxLayer.h"

namespace HIKARI::POST {

namespace {
constexpr int kMinViewportSize = 16;
constexpr int kMaxViewportSize = 8192;
bool gCameraDepthOfFieldPrepareWarningLogged = false;

void LogCameraDepthOfFieldPrepareFailure() {
    if (!gCameraDepthOfFieldPrepareWarningLogged) {
        HIKARI_LOG_WARN(
            "[CameraDepthOfField] Enabled effect could not prepare its render resources; using the regular transparent path.");
        gCameraDepthOfFieldPrepareWarningLogged = true;
    }
}

} // namespace

bool PostSystem::initialized_ = false;
GFX::Context PostSystem::context_{};
QuadDrawer PostSystem::quad_{};
SceneCaptureStage PostSystem::captureStage_{};
PreservedVfxLayer PostSystem::preservedVfxLayer_{};
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
RENDER3D::TEMPORAL::TemporalResourceFrame PostSystem::presentationTemporalResources_{};
RENDER3D::SURFACE::RenderFrameSurfaceReceipt PostSystem::renderSurfaceReceipt_{};
RENDER3D::PIPELINE::RenderFrameGraphSessionReceipt PostSystem::renderGraphSessionReceipt_{};

void PostSystem::Initialize(const GFX::Context& context) {
    UpdateContext(context);
    if (initialized_)
        return;
    if (!context_.device || !context_.cmdList) {
        DEBUGLOG::PushRenderError("[PostSystem][ERROR] Invalid graphics context.");
        return;
    }
    if (!quad_.Init(context_)) {
        DEBUGLOG::PushRenderError("[PostSystem][ERROR] QuadDrawer initialization failed.");
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
    AbortSceneCaptureTemporalFrame(RENDER3D::TEMPORAL::TemporalHistoryResetReason::PreviousFrameIncomplete);
    RENDER3D::DOF::ShutdownCameraDepthOfFieldStage();
    if (!initialized_)
        return;
    preservedVfxLayer_.Shutdown();
    captureStage_.Shutdown();
    processingStage_.Shutdown();
    gameUiCompositionStage_.Shutdown();
    presentationStage_.Shutdown();
    quad_.Finalize();
    initialized_ = false;
}

void PostSystem::UpdateCommonParams(float deltaTime) {
    if (!initialized_)
        Initialize(context_);
    int width = 0;
    int height = 0;
    GetSceneCaptureSize(width, height);
    processingStage_.UpdateCommonParams(deltaTime, width, height);
}

void PostSystem::AdvanceExposure(float deltaTime, const RENDER3D::COLOR::ExposureIllumination& illumination) {
    processingStage_.AdvanceExposure(deltaTime, illumination);
}

void PostSystem::SetSequenceEffects(std::span<const SEQUENCER::ScreenEffectSample> effects) {
    processingStage_.SetSequenceEffects(effects);
}
void PostSystem::SetIntensity(float value) { processingStage_.SetIntensity(value); }
void PostSystem::SetCombo(float value) { processingStage_.SetCombo(value); }
bool PostSystem::SetGlobalEffects(std::span<const ResolvedPostLayer> layers, const AssetRegistry* registry) {
    return processingStage_.SetGlobalEffects(layers, registry);
}
bool PostSystem::RequiresGlobalSurfaceFeatureMask() noexcept { return processingStage_.RequiresSurfaceFeatureMask(); }
bool PostSystem::SetActionPostFrame(uint64_t worldId, uint64_t frameIndex, std::string_view effectId,
                                    std::shared_ptr<const PostEffectDefinition> effect,
                                    const PostParameterBlock& values, const AssetRegistry& registry) {
    return initialized_ &&
           processingStage_.SetActionPostFrame(worldId, frameIndex, effectId, std::move(effect), values, registry);
}
void PostSystem::ClearActionPostFrame(uint64_t worldId) { processingStage_.ClearActionPostFrame(worldId); }
void PostSystem::SetActionPostView(uint64_t worldId, bool primaryGameView) noexcept {
    processingStage_.SetActionPostView(worldId, primaryGameView);
}
bool PostSystem::RequiresActionSurfaceFeatureMask() noexcept {
    return processingStage_.RequiresActionSurfaceFeatureMask();
}
bool PostSystem::SubmitEffectParameter(std::string_view effectId, uint32_t index, uint32_t channelMask,
                                       const DirectX::XMFLOAT4& value, int32_t priority, uint64_t sourceId) {
    return initialized_ &&
           processingStage_.SubmitEffectParameter(effectId, index, channelMask, value, priority, sourceId);
}
bool PostSystem::GetRadialScreenWarp(float& curvature, float& strength) noexcept {
    curvature = strength = 0.0f;
    if (!initialized_)
        return false;
    processingStage_.GetRadialScreenWarp(curvature, strength);
    return true;
}
void PostSystem::SetBloomSettings(const BloomSettings& settings) { processingStage_.SetBloomSettings(settings); }
void PostSystem::SetScreenReadability(const RENDER3D::ScreenReadabilityFrame& frame) {
    quad_.SetScreenReadability(frame);
}
void PostSystem::SetToneMappingSettings(const ToneMappingSettings& settings) {
    processingStage_.SetToneMappingSettings(settings);
}
void PostSystem::SetExposureSettings(const RENDER3D::COLOR::ExposureSettings& settings) {
    processingStage_.SetExposureSettings(settings);
}
void PostSystem::SetFxaaSettings(const FxaaSettings& settings) { processingStage_.SetFxaaSettings(settings); }
void PostSystem::SetSunGlareFrame(const SunGlareFrame& frame) { processingStage_.SetSunGlareFrame(frame); }
void PostSystem::ClearSunGlareFrame() { processingStage_.ClearSunGlareFrame(); }
void PostSystem::SetActionScreenImpulseFrame(const RENDER3D::COMBAT_VFX::PerfectGuardShockwaveFrameSource& source,
                                             const MATH::Mat4& viewProj, uint64_t frameIndex) {
    processingStage_.SetActionScreenImpulseFrame(source, viewProj, frameIndex);
}
void PostSystem::SetActionScreenDistortionMask(const PostSystem::ActionScreenDistortionMask& mask) {
    processingStage_.SetActionScreenDistortionMask(mask);
}
void PostSystem::SetActionScreenDistortionAccent(const PostSystem::ActionScreenDistortionAccent& accent) {
    processingStage_.SetActionScreenDistortionAccent(accent);
}
const PostSystem::FxaaSettings& PostSystem::GetFxaaSettings() { return processingStage_.GetFxaaSettings(); }
const PostSystem::BloomDebugStats& PostSystem::GetBloomDebugStats() { return processingStage_.GetBloomDebugStats(); }
const PostSystem::SunGlareDebugStats& PostSystem::GetSunGlareDebugStats() {
    return processingStage_.GetSunGlareDebugStats();
}
const PostSystem::ActionScreenImpulseDebugState& PostSystem::GetActionScreenImpulseDebugState() {
    return processingStage_.GetActionScreenImpulseDebugState();
}
const RENDER3D::COLOR::ExposureFrameState& PostSystem::GetExposureState() {
    return processingStage_.GetExposureState();
}
const RENDER3D::COLOR::AutomaticExposureDebugState& PostSystem::GetAutomaticExposureDebugState() {
    return processingStage_.GetAutomaticExposureDebugState();
}
void PostSystem::SetTransitionState(const TransitionVisualState& state) { processingStage_.SetTransitionState(state); }
void PostSystem::ClearTransitionState() { processingStage_.ClearTransitionState(); }
void PostSystem::RequestFrameDump() { processingStage_.RequestFrameDump(); }

std::string PostSystem::DumpFrameState() {
    const auto& quality = RENDER3D::GetRenderQualitySettings();
    std::ostringstream stream;
    stream << "[PostSystem] initialized=" << initialized_
           << " aaMode=" << RENDER3D::RenderAntiAliasingModeLabel(quality.antiAliasingMode) << "\n"
           << captureStage_.DumpState() << "\n"
           << processingStage_.DumpState() << "\n"
           << quad_.DumpState();
    return stream.str();
}

void PostSystem::LogFrameState(const char* reason) {
    DEBUGLOG::PushRenderError(std::string("[PostSystem][DUMP] reason=") + (reason ? reason : "") + "\n" +
                              DumpFrameState());
}

void PostSystem::SetSceneCaptureSize(int renderWidth, int renderHeight, int outputWidth, int outputHeight) {
    int nextRenderWidth = 0;
    int nextRenderHeight = 0;
    int nextOutputWidth = 0;
    int nextOutputHeight = 0;
    if (renderWidth <= 0 || renderHeight <= 0 || outputWidth <= 0 || outputHeight <= 0) {
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
    if (requestedRenderWidth_ == nextRenderWidth && requestedRenderHeight_ == nextRenderHeight &&
        requestedOutputWidth_ == nextOutputWidth && requestedOutputHeight_ == nextOutputHeight) {
        return;
    }
    requestedRenderWidth_ = nextRenderWidth;
    requestedRenderHeight_ = nextRenderHeight;
    requestedOutputWidth_ = nextOutputWidth;
    requestedOutputHeight_ = nextOutputHeight;
}

void PostSystem::GetSceneCaptureSize(int& width, int& height) {
    width = requestedRenderWidth_ > 0 ? requestedRenderWidth_ : PRESENTATION::kDefaultLogicalWidth;
    height = requestedRenderHeight_ > 0 ? requestedRenderHeight_ : PRESENTATION::kDefaultLogicalHeight;
}

void PostSystem::GetSceneOutputSize(int& width, int& height) {
    width = requestedOutputWidth_ > 0
                ? requestedOutputWidth_
                : (requestedRenderWidth_ > 0 ? requestedRenderWidth_ : PRESENTATION::kDefaultLogicalWidth);
    height = requestedOutputHeight_ > 0
                 ? requestedOutputHeight_
                 : (requestedRenderHeight_ > 0 ? requestedRenderHeight_ : PRESENTATION::kDefaultLogicalHeight);
}

bool PostSystem::BeginSceneCapture(RENDER3D::RenderFrameToken frame, uint64_t deviceGeneration) {
    quad_.SetScreenReadability({});
    if (!frame.IsValid() || frame.viewId != RENDER3D::kPrimaryRenderViewId ||
        frame.frameIndex != TIME::GetFrameContext().frameIndex || deviceGeneration == 0u ||
        deviceGeneration != context_.deviceGeneration) {
        return false;
    }
    if (!AbortSceneCaptureTemporalFrame(RENDER3D::TEMPORAL::TemporalHistoryResetReason::PreviousFrameIncomplete)) {
        return false;
    }
    if (!initialized_)
        Initialize(context_);
    RENDER3D::SURFACE::RenderFrameSurfaceReceipt receipt{};
    if (!initialized_ || !RENDER3D::SURFACE::BeginRenderFrameSurfaces(frame, deviceGeneration, receipt)) {
        return false;
    }
    int width = 0;
    int height = 0;
    GetSceneCaptureSize(width, height);
    processingStage_.ClearSunGlareFrame();
    processingStage_.UpdateCommonParams(0.0f, width, height);
    if (!captureStage_.Begin(width, height)) {
        (void)RENDER3D::SURFACE::AbortRenderFrameSurfaces(receipt);
        LogFrameState("BeginSceneCapture failed");
        return false;
    }
    renderSurfaceReceipt_ = receipt;
    return true;
}

RENDER3D::SURFACE::RenderFrameSurfaceReceipt PostSystem::GetSceneCaptureSurfaceReceipt() noexcept {
    return renderSurfaceReceipt_;
}

bool PostSystem::SetSceneCaptureGraphSession(RENDER3D::PIPELINE::RenderFrameGraphSessionReceipt session) {
    if (!session.IsValid() || !renderSurfaceReceipt_.IsValid() || !session.surfaces.Matches(renderSurfaceReceipt_) ||
        renderGraphSessionReceipt_.IsValid() || !RENDER3D::PIPELINE::IsRenderFrameGraphSessionCurrent(session)) {
        return false;
    }
    renderGraphSessionReceipt_ = session;
    return true;
}

bool PostSystem::ContinueSceneCaptureGraphSession() {
    if (!renderGraphSessionReceipt_.IsValid()) {
        return true;
    }
    const auto session = renderGraphSessionReceipt_;
    if (!RENDER3D::PIPELINE::ContinueRenderFrameGraphSession(session)) {
        if (RENDER3D::PIPELINE::AbortRenderFrameGraphSession(session)) {
            renderGraphSessionReceipt_ = {};
        }
        return false;
    }
    if (!RENDER3D::PIPELINE::EndRenderFrameGraphSession(session)) {
        if (RENDER3D::PIPELINE::AbortRenderFrameGraphSession(session)) {
            renderGraphSessionReceipt_ = {};
        }
        return false;
    }
    renderGraphSessionReceipt_ = {};
    return true;
}

bool PostSystem::SetSceneCaptureTemporalFrame(RENDER3D::TEMPORAL::TemporalViewLease view,
                                              RENDER3D::RenderFrameToken frame,
                                              RENDER3D::TEMPORAL::TemporalResourceFrame resources) {
    if (!view.IsValid() || !frame.IsValid() || !resources.IsValid() || view.viewId != frame.viewId ||
        frame.viewId != RENDER3D::kPrimaryRenderViewId || !captureStage_.IsActive() ||
        !renderSurfaceReceipt_.IsValid() || !renderSurfaceReceipt_.frame.Matches(frame)) {
        return false;
    }
    if (presentationTemporalResources_.IsValid()) {
        return false;
    }
    if (!resources.view.Matches(view) || !resources.publication.Matches(frame) ||
        resources.deviceGeneration != renderSurfaceReceipt_.deviceGeneration) {
        return false;
    }
    if (temporalFrame_.IsValid()) {
        if (!temporalView_.Matches(view) || !temporalFrame_.Matches(frame)) {
            return false;
        }
        if (!temporalResources_.IsValid()) {
            temporalResources_ = resources;
            return true;
        }
        return resources.IsValid() && temporalResources_.Matches(resources);
    }
    temporalView_ = view;
    temporalFrame_ = frame;
    temporalResources_ = resources;
    return true;
}

bool PostSystem::AbortSceneCaptureTemporalFrame(RENDER3D::TEMPORAL::TemporalHistoryResetReason reason) {
    preservedVfxLayer_.Invalidate();
    bool closed = true;
    if (renderGraphSessionReceipt_.IsValid()) {
        if (RENDER3D::PIPELINE::AbortRenderFrameGraphSession(renderGraphSessionReceipt_)) {
            renderGraphSessionReceipt_ = {};
        } else {
            closed = false;
        }
    }
    if (presentationTemporalResources_.IsValid()) {
        if (RENDER3D::TEMPORAL::AbortTemporalResources(presentationTemporalResources_, reason)) {
            presentationTemporalResources_ = {};
        } else {
            closed = false;
        }
    }
    if (temporalResources_.IsValid()) {
        if (RENDER3D::TEMPORAL::AbortTemporalResources(temporalResources_, reason)) {
            temporalView_ = {};
            temporalFrame_ = {};
            temporalResources_ = {};
        } else {
            closed = false;
        }
    } else if (temporalView_.IsValid() && temporalFrame_.IsValid()) {
        bool cpuClosed = RENDER3D::TEMPORAL::AbortTemporalFrame(temporalView_, temporalFrame_, reason);
        if (!cpuClosed) {
            RENDER3D::TEMPORAL::TemporalFrameState state{};
            cpuClosed =
                RENDER3D::TEMPORAL::TryGetTemporalFrameState(temporalView_, temporalFrame_, state) && state.completed;
        }
        if (cpuClosed) {
            temporalView_ = {};
            temporalFrame_ = {};
        } else {
            closed = false;
        }
    }
    if (captureStage_.IsActive() && !captureStage_.Abort()) {
        closed = false;
    }
    if (renderSurfaceReceipt_.IsValid()) {
        const bool dependentsClosed = !renderGraphSessionReceipt_.IsValid() &&
                                      !presentationTemporalResources_.IsValid() && !temporalResources_.IsValid() &&
                                      !temporalView_.IsValid() && !temporalFrame_.IsValid() &&
                                      !captureStage_.IsActive();
        if (dependentsClosed && RENDER3D::SURFACE::AbortRenderFrameSurfaces(renderSurfaceReceipt_)) {
            renderSurfaceReceipt_ = {};
        } else {
            closed = false;
        }
    }
    return closed;
}

bool PostSystem::TryGetPresentationTemporalInputs(RENDER3D::TEMPORAL::TemporalInputs& outInputs) {
    return RENDER3D::TEMPORAL::TryGetTemporalResolveInputs(presentationTemporalResources_, outInputs);
}

bool PostSystem::FinishPresentationTemporalFrame(bool succeeded) {
    if (renderGraphSessionReceipt_.IsValid()) {
        (void)AbortSceneCaptureTemporalFrame(RENDER3D::TEMPORAL::TemporalHistoryResetReason::PreviousFrameIncomplete);
        return false;
    }
    bool temporalClosed = true;
    bool temporalCommitted = succeeded;
    if (presentationTemporalResources_.IsValid()) {
        const auto resources = presentationTemporalResources_;
        if (succeeded) {
            temporalClosed = RENDER3D::TEMPORAL::EndTemporalResources(resources);
            if (!temporalClosed) {
                temporalCommitted = false;
                temporalClosed = RENDER3D::TEMPORAL::AbortTemporalResources(
                    resources, RENDER3D::TEMPORAL::TemporalHistoryResetReason::PreviousFrameIncomplete);
            }
        } else {
            temporalCommitted = false;
            temporalClosed = RENDER3D::TEMPORAL::AbortTemporalResources(
                resources, RENDER3D::TEMPORAL::TemporalHistoryResetReason::PreviousFrameIncomplete);
        }
        if (temporalClosed) {
            presentationTemporalResources_ = {};
        }
    }

    bool surfacesClosed = true;
    if (renderSurfaceReceipt_.IsValid()) {
        surfacesClosed = temporalClosed && temporalCommitted
                             ? RENDER3D::SURFACE::EndRenderFrameSurfaces(renderSurfaceReceipt_)
                             : RENDER3D::SURFACE::AbortRenderFrameSurfaces(renderSurfaceReceipt_);
        if (surfacesClosed) {
            renderSurfaceReceipt_ = {};
        }
    }
    return temporalClosed && surfacesClosed;
}

bool PostSystem::HasCurrentRenderTarget() { return initialized_ && captureStage_.HasCurrentTarget(); }
bool PostSystem::RebindCurrentRenderTarget() { return captureStage_.RebindCurrentTarget(); }
bool PostSystem::BindAdditionalColors(std::span<const D3D12_CPU_DESCRIPTOR_HANDLE> colors) {
    return captureStage_.BindAdditionalColors(colors);
}
bool PostSystem::ClearCurrentRenderTargetColor(float red, float green, float blue, float alpha) {
    return initialized_ && captureStage_.ClearCurrentColor(red, green, blue, alpha);
}
bool PostSystem::CopyToCurrentRenderTargetColor(ID3D12Resource* source, DXGI_FORMAT format, uint32_t width,
                                                uint32_t height) {
    return initialized_ && captureStage_.CopyToCurrentColor(source, format, width, height);
}
bool PostSystem::RebindPresentationBackBuffer() {
    if (!initialized_ || context_.cmdList == nullptr || context_.rtv.ptr == 0u || context_.backBufferWidth <= 0 ||
        context_.backBufferHeight <= 0) {
        return false;
    }
    presentationStage_.BindBackBufferFullViewport();
    return true;
}
RENDER3D::RenderTextureView PostSystem::GetCurrentRenderTargetDepthView() {
    return captureStage_.GetCurrentDepthView();
}
RENDER3D::RenderTextureView PostSystem::GetCurrentRenderTargetColorView() {
    return captureStage_.GetCurrentColorView();
}
D3D12_CPU_DESCRIPTOR_HANDLE PostSystem::GetCurrentRenderTargetDsv() { return captureStage_.GetCurrentDsv(); }
D3D12_CPU_DESCRIPTOR_HANDLE PostSystem::GetCurrentRenderTargetReadOnlyDsv() {
    return captureStage_.GetCurrentReadOnlyDsv();
}
bool PostSystem::BeginPreservedVfxCapture(const RENDER3D::PIPELINE::RenderFrameContext& frame,
                                          const RENDER3D::TEMPORAL::TemporalFrameState& temporal,
                                          RENDER3D::PIPELINE::RenderTargetAccess& target) {
    return initialized_ && captureStage_.IsActive() && frame.surfaceReceipt.Matches(renderSurfaceReceipt_) &&
           preservedVfxLayer_.Begin(frame, temporal, target);
}
bool PostSystem::EndPreservedVfxCapture(bool succeeded) { return preservedVfxLayer_.End(succeeded); }
bool PostSystem::BeginCurrentRenderTargetDepthRead() { return captureStage_.BeginCurrentDepthRead(); }
void PostSystem::EndCurrentRenderTargetDepthRead() { captureStage_.EndCurrentDepthRead(); }

bool PostSystem::PrepareCameraDepthOfField(uint64_t frameIndex, const CAMERA::DepthOfFieldSettings& settings,
                                           RENDER3D::DepthOfFieldQuality quality, bool captureWorldSprites) {
    if (!initialized_) {
        if (RENDER3D::DOF::IsCameraDepthOfFieldActive(settings, quality)) {
            LogCameraDepthOfFieldPrepareFailure();
        }
        return false;
    }
    const bool compositeSpriteFog = captureWorldSprites && RENDER3D::VOLUMETRIC::IsVolumetricLightingActive();
    if (!RENDER3D::DOF::IsCameraDepthOfFieldActive(settings, quality) && !compositeSpriteFog) {
        gCameraDepthOfFieldPrepareWarningLogged = false;
        return RENDER3D::DOF::PrepareCameraDepthOfFieldFrame(context_, frameIndex, {}, {}, settings, quality, false,
                                                             false);
    }
    if (!captureStage_.HasCurrentTarget()) {
        LogCameraDepthOfFieldPrepareFailure();
        return false;
    }
    const RENDER3D::RenderTextureView sceneColor = captureStage_.GetCurrentColorView();
    const RENDER3D::RenderTextureView sceneDepth = captureStage_.GetCurrentDepthView();
    const bool prepared = RENDER3D::DOF::PrepareCameraDepthOfFieldFrame(
        context_, frameIndex, sceneColor, sceneDepth, settings, quality, captureWorldSprites, compositeSpriteFog);
    if (prepared) {
        gCameraDepthOfFieldPrepareWarningLogged = false;
    } else {
        LogCameraDepthOfFieldPrepareFailure();
    }
    return prepared;
}

bool PostSystem::ExecuteCameraDepthOfField(uint64_t frameIndex,
                                           const RENDER3D::TEMPORAL::TemporalFrameState& temporalFrame,
                                           const Camera3D& camera, const CAMERA::DepthOfFieldSettings& settings,
                                           RENDER3D::DepthOfFieldQuality quality) {
    if (!initialized_) {
        return false;
    }
    RenderTarget2D* target = captureStage_.GetCurrentTarget();
    if (target == nullptr) {
        HIKARI_LOG_ERROR("[CameraDepthOfField] Prepared frame lost its scene render target before composite.");
        return false;
    }
    (void)RENDER3D::VOLUMETRIC::ExecuteVolumetricLightingStage(*target);
    RENDER3D::VOLUMETRIC::VolumetricFogSamplingView fogView{};
    (void)RENDER3D::VOLUMETRIC::GetVolumetricFogSamplingView(frameIndex, fogView);
    if (!captureStage_.BeginCurrentDepthRead()) {
        HIKARI_LOG_ERROR("[CameraDepthOfField] Prepared frame could not transition scene depth for compute sampling.");
        (void)captureStage_.RebindCurrentTarget();
        return false;
    }
    const RENDER3D::RenderTextureView depth = captureStage_.GetCurrentDepthView();
    const bool executed = RENDER3D::DOF::ExecuteCameraDepthOfField(context_, frameIndex, *target, depth, temporalFrame,
                                                                   camera, settings, quality, fogView);
    captureStage_.EndCurrentDepthRead();
    const bool rebound = captureStage_.RebindCurrentTarget();
    if (!executed || !rebound) {
        HIKARI_LOG_ERROR("[CameraDepthOfField] A prepared frame failed during composite after sprite routing.");
    }
    return executed && rebound;
}

RENDER3D::RenderTextureView PostSystem::CaptureSceneColorSnapshot(SceneColorSnapshotKind kind) {
    return captureStage_.CaptureSceneColorSnapshot(kind);
}
RENDER3D::RenderTextureView PostSystem::GetSceneColorSnapshotView(SceneColorSnapshotKind kind) {
    return captureStage_.GetSceneColorSnapshotView(kind);
}

bool PostSystem::IsEditorViewportReady() {
    return presentationStage_.IsEditorOutputReady(requestedOutputWidth_, requestedOutputHeight_);
}
D3D12_GPU_DESCRIPTOR_HANDLE PostSystem::GetEditorViewportSrv() { return presentationStage_.GetEditorOutputSrv(); }

void PostSystem::SetAmbientColor(float r, float g, float b) { captureStage_.SetAmbientColor(r, g, b); }
void PostSystem::BeginLightCapture() { captureStage_.BeginLightCapture(); }
void PostSystem::EndLightCapture() { captureStage_.EndLightCapture(); }

RenderTarget2D* PostSystem::EndSceneCaptureAndResolveFinal() {
    if (!initialized_) {
        AbortSceneCaptureTemporalFrame(RENDER3D::TEMPORAL::TemporalHistoryResetReason::PreviousFrameIncomplete);
        return nullptr;
    }
    GFX::GPU_PROFILE::ScopedGpuTimer timer(context_.cmdList, GFX::GPU_PROFILE::Pass::PostResolve);
    RenderTarget2D* captured = captureStage_.End();
    if (!captured) {
        AbortSceneCaptureTemporalFrame(RENDER3D::TEMPORAL::TemporalHistoryResetReason::PreviousFrameIncomplete);
        return nullptr;
    }

    RenderTarget2D* volumetric = RENDER3D::VOLUMETRIC::ExecuteVolumetricLightingStage(*captured);
    if (volumetric == nullptr || volumetric->GetResource() == nullptr) {
        volumetric = captured;
    }

    const auto& quality = RENDER3D::GetRenderQualitySettings();
    const auto handoffView = temporalView_;
    const auto handoffFrame = temporalFrame_;
    const auto handoffResources = temporalResources_;
    RENDER3D::TEMPORAL::TemporalFrameState capturedTemporal{};
    (void)RENDER3D::TEMPORAL::TryGetTemporalFrameState(handoffView, handoffFrame, capturedTemporal);
    auto temporal = RENDER3D::TEMPORAL::ExecuteTemporalResolveStage(handoffView, handoffFrame, handoffResources,
                                                                    context_, *volumetric, *captured, quality,
                                                                    processingStage_.GetExposureState());
    RENDER3D::TEMPORAL::TemporalInputs presentationInputs{};
    bool retainHandoff = false;
    if (handoffResources.IsValid() &&
        RENDER3D::TEMPORAL::TryGetTemporalResolveInputs(handoffResources, presentationInputs)) {
        presentationTemporalResources_ = handoffResources;
    } else if (handoffResources.IsValid()) {
        retainHandoff = !RENDER3D::TEMPORAL::AbortTemporalResources(
            handoffResources, RENDER3D::TEMPORAL::TemporalHistoryResetReason::PreviousFrameIncomplete);
    }
    if (!retainHandoff) {
        (void)RENDER3D::TEMPORAL::AbortTemporalFrame(
            handoffView, handoffFrame, RENDER3D::TEMPORAL::TemporalHistoryResetReason::PreviousFrameIncomplete);
        temporalView_ = {};
        temporalFrame_ = {};
        temporalResources_ = {};
    }
    RenderTarget2D* output = temporal.output ? temporal.output : volumetric;
    if (temporal.requiresOutputNormalization) {
        RenderTarget2D* normalized = processingStage_.NormalizeTemporalOutput(*output, temporal.expectedOutputWidth,
                                                                              temporal.expectedOutputHeight, quad_);
        if (normalized) {
            output = normalized;
        } else {
            output = volumetric;
            temporal.outputJitterPixels = {capturedTemporal.camera.jitter.x, capturedTemporal.camera.jitter.y};
            RENDER3D::TEMPORAL::TemporalViewLease temporalView{};
            if (RENDER3D::TEMPORAL::AcquireTemporalView(RENDER3D::kPrimaryRenderViewId, temporalView)) {
                (void)RENDER3D::TEMPORAL::ResetTemporalFrameHistory(
                    temporalView, RENDER3D::TEMPORAL::TemporalHistoryResetReason::ExplicitReset);
            }
        }
    }
    const RENDER3D::RenderTextureView* featureMask = nullptr;
    if ((processingStage_.RequiresSurfaceFeatureMask() || processingStage_.RequiresActionSurfaceFeatureMask()) &&
        renderSurfaceReceipt_.frame.Matches(handoffFrame) &&
        renderSurfaceReceipt_.deviceGeneration == context_.deviceGeneration) {
        if (const auto* surfaces = RENDER3D::SURFACE::TryGetRenderFrameSurfaces(renderSurfaceReceipt_))
            featureMask = surfaces->TryGetTexture({RENDER3D::GRAPH::RenderGraphSemantic::SurfaceFeatureMask,
                                                   RENDER3D::GRAPH::RenderGraphSemanticPhase::PostOpaque});
    }
    RenderTarget2D* preserved = nullptr;
    if (!preservedVfxLayer_.Resolve(renderSurfaceReceipt_, temporal.outputJitterPixels, output->GetWidth(),
                                    output->GetHeight(), quad_, preserved)) {
        HIKARI_LOG_ERROR("[PreservedVfx] Current layer could not resolve to the presentation raster.");
        return nullptr;
    }
    return processingStage_.ResolveHdr(*output, quad_, RENDER3D::GetRenderDebugOutputRoute().bypassPostProcessing,
                                       RENDER3D::TEMPORAL::ToString(temporal.backend),
                                       captureStage_.IsLightingEnabled(), captured, featureMask, preserved);
}

RenderTarget2D* PostSystem::ResolveFinalSceneToLdr(RenderTarget2D& source, DXGI_FORMAT outputFormat) {
    if (RENDER3D::GetRenderDebugOutputRoute().bypassPostProcessing) {
        return processingStage_.ResolveDebugLdr(source, outputFormat, quad_);
    }
    return processingStage_.ResolveLdr(source, outputFormat, quad_, captureStage_.GetLightTarget());
}

bool PostSystem::PrepareGameUiComposition(bool editorViewport, int uiLogicalWidth, int uiLogicalHeight) {
    if (gameUiCompositionStage_.IsActive()) {
        return true;
    }
    if (!captureStage_.IsActive()) {
        return false;
    }

    RenderTarget2D* hdr = EndSceneCaptureAndResolveFinal();
    if (!hdr || !context_.cmdList) {
        presentationStage_.BindBackBufferFullViewport();
        return false;
    }

    RenderTarget2D* ldr = ResolveFinalSceneToLdr(*hdr, DXGI_FORMAT_R8G8B8A8_UNORM);
    if (!ldr) {
        presentationStage_.BindBackBufferFullViewport();
        return false;
    }

    int outputWidth = editorViewport ? requestedOutputWidth_ : context_.backBufferWidth;
    int outputHeight = editorViewport ? requestedOutputHeight_ : context_.backBufferHeight;
    if (outputWidth <= 0 || outputHeight <= 0) {
        outputWidth = ldr->GetWidth();
        outputHeight = ldr->GetHeight();
    }

    const int logicalWidth = uiLogicalWidth > 0 ? uiLogicalWidth : outputWidth;
    const int logicalHeight = uiLogicalHeight > 0 ? uiLogicalHeight : outputHeight;
    return gameUiCompositionStage_.Begin(*ldr, quad_, outputWidth, outputHeight, logicalWidth, logicalHeight,
                                         TIME::GetFrameContext().frameIndex, !editorViewport);
}

bool PostSystem::FinalizeGameUiComposition(bool editorViewport) {
    if (!gameUiCompositionStage_.IsActive()) {
        return false;
    }

    RenderTarget2D* finalColor = gameUiCompositionStage_.End(quad_);
    if (finalColor == nullptr) {
        presentationStage_.BindBackBufferFullViewport();
        return false;
    }

    if (editorViewport) {
        GFX::GPU_PROFILE::ScopedGpuTimer timer(context_.cmdList, GFX::GPU_PROFILE::Pass::GameViewResolve);
        return presentationStage_.PresentToEditor(*finalColor, quad_, requestedOutputWidth_, requestedOutputHeight_);
    }

    return presentationStage_.PresentToBackBuffer(*finalColor, quad_);
}

const PresentationFrameResources& PostSystem::GetPresentationFrameResources() {
    return gameUiCompositionStage_.GetFrameResources();
}

} // namespace HIKARI::POST
