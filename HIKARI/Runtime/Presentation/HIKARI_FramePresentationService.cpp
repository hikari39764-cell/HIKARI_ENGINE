#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Runtime/Presentation/HIKARI_GamePresentationService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Runtime/Services/Internal/HIKARI_RuntimeServiceState.h"

#include <cmath>
#include <sstream>
#include <string>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render2D/HIKARI_Camera.h"
#include "Render2D/HIKARI_DxRenderer.h"
#include "Render3D/Ui/HIKARI_GameUiEffectRenderer.h"
#include "RenderCore/Texture/HIKARI_DxTexture.h"
#include "Render2D/HIKARI_Renderer.h"
#include "Render3D/Color/HIKARI_ViewExposure.h"
#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Core/HIKARI_RenderFrameToken.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenSpacePasses.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGenerationPolicy.h"
#include "Render3D/Upscaling/HIKARI_StreamlineReflex.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Runtime/Host/HIKARI_HostBootstrapConfig.h"
#include "Runtime/Host/HIKARI_IEditorUiServices.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectCameraLensConversion.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI {
namespace SERVICES {
namespace {
auto& gGraphicsCore = INTERNAL::GetRuntimeServiceState().core;
auto& gWindow = INTERNAL::GetRuntimeServiceState().window;
auto& gInputService = INTERNAL::GetRuntimeServiceState().inputService;
auto& gActionTimeDilationService = INTERNAL::GetRuntimeServiceState().actionTimeDilationService;
auto& gGamePresentationController = INTERNAL::GetRuntimeServiceState().gamePresentationController;
auto& gGpuFrameReady = INTERNAL::GetRuntimeServiceState().gpuFrameReady;
auto& gPresentationPrepared = INTERNAL::GetRuntimeServiceState().presentationPrepared;
auto& gSceneLayersRendered = INTERNAL::GetRuntimeServiceState().sceneLayersRendered;
auto& gHasPendingWindowResize = INTERNAL::GetRuntimeServiceState().hasPendingWindowResize;
auto& gPendingWindowWidth = INTERNAL::GetRuntimeServiceState().pendingWindowWidth;
auto& gPendingWindowHeight = INTERNAL::GetRuntimeServiceState().pendingWindowHeight;
auto& gLogicalScreenWidth = INTERNAL::GetRuntimeServiceState().logicalScreenWidth;
auto& gLogicalScreenHeight = INTERNAL::GetRuntimeServiceState().logicalScreenHeight;
auto& gEditorGameViewportVisible = INTERNAL::GetRuntimeServiceState().editorGameViewportVisible;
auto& gEditorGameViewportWidth = INTERNAL::GetRuntimeServiceState().editorGameViewportWidth;
auto& gEditorGameViewportHeight = INTERNAL::GetRuntimeServiceState().editorGameViewportHeight;
auto& gStreamlineEnabled = INTERNAL::GetRuntimeServiceState().streamlineEnabled;
auto& gRenderQualityRuntimeInitialized = INTERNAL::GetRuntimeServiceState().renderQualityRuntimeInitialized;
auto& gAppliedRenderQuality = INTERNAL::GetRuntimeServiceState().appliedRenderQuality;
auto& gRenderQualityTransitionCount = INTERNAL::GetRuntimeServiceState().renderQualityTransitionCount;

struct PendingPrimaryCameraFrame {
    RENDER3D::ResolvedCameraFrame frame{};
    uint64_t sourceCameraObjectHandle = 0u;
    uint64_t frameIndex = 0u;
    uint64_t worldIdentity = 0u;
    GameProjectPrimaryViewSource source = GameProjectPrimaryViewSource::Game;
    bool valid = false;
};

PendingPrimaryCameraFrame gPendingPrimaryCameraFrame{};
GameProjectCameraViewSnapshot gLatestPrimaryViewSnapshot{};
bool gHasLatestPrimaryViewSnapshot = false;
FramePresentationDestination gPreparedPresentationDestination = FramePresentationDestination::BackBuffer;

GameProjectFloat3 ToProject(const MATH::Vec3& value) noexcept { return {value.x, value.y, value.z}; }

bool CopyPresentedLens(const RENDER3D::ResolvedCameraFrame& frame, uint64_t sourceCameraObjectHandle,
                       GameProjectCameraLens& outLens, GameProjectCameraViewPose& outPose, float& outAspect) noexcept {
    if (!frame.valid) {
        return false;
    }
    const Camera3D& camera = frame.camera;
    const MATH::Vec3 eye = camera.GetPosition();
    const MATH::Vec3 target = camera.GetTarget();
    const MATH::Vec3 up = camera.GetUp();
    const CAMERA::DepthOfFieldSettings& optics = camera.GetDepthOfFieldSettings();
    if (!MATH::IsFinite(eye) || !MATH::IsFinite(target) || !MATH::IsFinite(up) || !std::isfinite(camera.GetAspect()) ||
        camera.GetAspect() <= 0.0f) {
        return false;
    }
    if (!PROJECT::INTERNAL::CopyCameraLens(outLens, sourceCameraObjectHandle, GameProjectCameraLensStage::Rendered,
                                           camera.IsOrthographic() ? GameProjectCameraProjectionKind::Orthographic
                                                                   : GameProjectCameraProjectionKind::Perspective,
                                           camera.GetFovYRad(), camera.GetOrthographicHeight(), camera.GetNearZ(),
                                           camera.GetFarZ(), optics)) {
        return false;
    }
    outPose.eye = ToProject(eye);
    outPose.target = ToProject(target);
    outPose.up = ToProject(up);
    outAspect = camera.GetAspect();
    return true;
}

bool BuildPresentedViewSnapshot(const POST::PresentationFrameResources& presentation,
                                GameProjectPresentationTarget presentationTarget,
                                GameProjectCameraViewSnapshot& outSnapshot) noexcept {
    if (!gPendingPrimaryCameraFrame.valid || !gPendingPrimaryCameraFrame.frame.valid ||
        gPendingPrimaryCameraFrame.frameIndex != presentation.frameIndex || presentation.width <= 0 ||
        presentation.height <= 0 || presentation.uiLogicalWidth <= 0 || presentation.uiLogicalHeight <= 0 ||
        presentation.sceneRect.right <= presentation.sceneRect.left ||
        presentation.sceneRect.bottom <= presentation.sceneRect.top || !presentation.finalReady) {
        return false;
    }

    int sceneRenderWidth = 0;
    int sceneRenderHeight = 0;
    POST::PostSystem::GetSceneCaptureSize(sceneRenderWidth, sceneRenderHeight);
    if (sceneRenderWidth <= 0 || sceneRenderHeight <= 0) {
        return false;
    }

    GameProjectCameraViewSnapshot snapshot{};
    if (!CopyPresentedLens(gPendingPrimaryCameraFrame.frame, gPendingPrimaryCameraFrame.sourceCameraObjectHandle,
                           snapshot.lens, snapshot.pose, snapshot.aspectRatio)) {
        return false;
    }
    snapshot.frameIndex = presentation.frameIndex;
    snapshot.worldIdentity = gPendingPrimaryCameraFrame.worldIdentity;
    snapshot.primaryViewId = RENDER3D::kPrimaryRenderViewId.value;
    snapshot.source = gPendingPrimaryCameraFrame.source;
    snapshot.presentationTarget = presentationTarget;
    snapshot.sceneRenderWidth = static_cast<uint32_t>(sceneRenderWidth);
    snapshot.sceneRenderHeight = static_cast<uint32_t>(sceneRenderHeight);
    snapshot.outputPhysicalWidth = static_cast<uint32_t>(presentation.width);
    snapshot.outputPhysicalHeight = static_cast<uint32_t>(presentation.height);
    snapshot.uiLogicalWidth = static_cast<uint32_t>(presentation.uiLogicalWidth);
    snapshot.uiLogicalHeight = static_cast<uint32_t>(presentation.uiLogicalHeight);
    snapshot.sceneContentPhysical = {
        static_cast<float>(presentation.sceneRect.left), static_cast<float>(presentation.sceneRect.top),
        static_cast<float>(presentation.sceneRect.right), static_cast<float>(presentation.sceneRect.bottom)};
    const float logicalScaleX =
        static_cast<float>(presentation.uiLogicalWidth) / static_cast<float>(presentation.width);
    const float logicalScaleY =
        static_cast<float>(presentation.uiLogicalHeight) / static_cast<float>(presentation.height);
    snapshot.sceneContentLogical = {
        snapshot.sceneContentPhysical.left * logicalScaleX, snapshot.sceneContentPhysical.top * logicalScaleY,
        snapshot.sceneContentPhysical.right * logicalScaleX, snapshot.sceneContentPhysical.bottom * logicalScaleY};
    outSnapshot = snapshot;
    return true;
}

void AbortActivePrimaryTemporalFrame() {
    POST::PostSystem::AbortSceneCaptureTemporalFrame(
        RENDER3D::TEMPORAL::TemporalHistoryResetReason::PreviousFrameIncomplete);
}

void CloseFailedBeginFrame() {
    HIKARI::RENDERER::ClearSubmittedCommands();
    AbortActivePrimaryTemporalFrame();
    RENDER3D::UPSCALING::EndStreamlineReflexSimulation();
    gGraphicsCore.SetVSyncEnabled(RENDER3D::GetRenderQualitySettings().vSync);
    if (!gGraphicsCore.EndFrame()) {
        HIKARI_LOG_ERROR("D3D12 failed to close an aborted begin-frame.");
    }
    (void)RENDER3D::UPSCALING::CaptureStreamlineFrameGenerationCompletionAfterPresent();
    gGpuFrameReady = false;
    GFX::PIX::Update();
    CPU_PROFILE::EndFrame();
}

std::string DescribeRenderQualityTransition(const RENDER3D::RenderQualitySettings& before,
                                            const RENDER3D::RenderQualitySettings& after,
                                            const RENDER3D::RenderQualityRuntimeChangeSet& changes) {
    std::ostringstream stream;
    stream << "[RenderQuality] hot transition #" << gRenderQualityTransitionCount
           << " aa=" << RENDER3D::RenderAntiAliasingModeLabel(before.antiAliasingMode) << "->"
           << RENDER3D::RenderAntiAliasingModeLabel(after.antiAliasingMode)
           << " dlss=" << RENDER3D::DlssQualityModeLabel(before.dlssQualityMode) << "->"
           << RENDER3D::DlssQualityModeLabel(after.dlssQualityMode)
           << " scene=" << RENDER3D::RenderResolutionPresetLabel(before.sceneResolution) << "->"
           << RENDER3D::RenderResolutionPresetLabel(after.sceneResolution)
           << " drain=" << (changes.RequiresGpuDrain() ? "yes" : "no")
           << " temporalReset=" << (changes.RequiresTemporalReset() ? "yes" : "no")
           << " streamlineReset=" << (changes.RequiresStreamlineReset() ? "yes" : "no");
    return stream.str();
}

bool SynchronizeRenderQualityRuntime() {
    auto& service = INTERNAL::GetRuntimeServiceState();
    if (service.hasRequestedRenderQuality) {
        service.hasRequestedRenderQuality = false;
        if (IsGamePresentationActive())
            RENDER3D::SetRenderQualitySettings(service.requestedRenderQuality);
    }
    const RENDER3D::RenderQualitySettings requested = RENDER3D::GetRenderQualitySettings();
    if (!gRenderQualityRuntimeInitialized) {
        gAppliedRenderQuality = requested;
        gRenderQualityRuntimeInitialized = true;
        return true;
    }
    if (gAppliedRenderQuality == requested) {
        return true;
    }

    const RENDER3D::RenderQualitySettings previous = gAppliedRenderQuality;
    RENDER3D::RenderQualitySettings effective = requested;
    RENDER3D::RenderQualityRuntimeChangeSet changes =
        RENDER3D::ClassifyRenderQualityRuntimeChanges(previous, effective);

    if (changes.presentationChanged && IsGamePresentationActive() && !ApplyWindowPresentationSettings()) {
        effective.windowSize = previous.windowSize;
        effective.windowMode = previous.windowMode;
        RENDER3D::SetRenderQualitySettings(effective);
        HIKARI_LOG_WARN(
            "[RenderQuality] live window presentation change was rejected; previous presentation settings were restored.");
        changes = RENDER3D::ClassifyRenderQualityRuntimeChanges(previous, effective);
    }

    if (!changes.RequiresGpuDrain()) {
        gAppliedRenderQuality = effective;
        ++gRenderQualityTransitionCount;
        HIKARI_LOG_INFO(DescribeRenderQualityTransition(previous, effective, changes));
        return true;
    }

    // Resolution and Streamline resources can be referenced by all
    // three in-flight frame slots.  Reconfigure only after the
    // presentation owner proves GPU idle; the swap chain itself is
    // deliberately kept alive.
    if (!gGraphicsCore.WaitForIdle()) {
        RENDER3D::SetRenderQualitySettings(previous);
        HIKARI_LOG_ERROR("[RenderQuality] GPU idle could not be proven; live quality change was rolled back.");
        return true;
    }

    if (changes.RequiresStreamlineReset() && gStreamlineEnabled &&
        !RENDER3D::UPSCALING::ReleaseStreamlineTransientResources()) {
        // A failed Streamline resource release must never leave the
        // frame routed into a black DLSS target.  Keep the editor
        // usable with the native temporal backend until a later
        // retry or process restart.
        effective.antiAliasingMode = RENDER3D::RenderAntiAliasingMode::TAA;
        effective.frameGenerationMode = RENDER3D::RenderFrameGenerationMode::Off;
        RENDER3D::SetRenderQualitySettings(effective);
        HIKARI_LOG_ERROR(
            "[RenderQuality] Streamline hot reset failed; falling back to TAA with frame generation disabled.");
        changes = RENDER3D::ClassifyRenderQualityRuntimeChanges(previous, effective);
    }

    RENDER3D::TEMPORAL::ShutdownTemporalResourceSystem(
        RENDER3D::TEMPORAL::TemporalResourceReleaseMode::ImmediateAfterGpuIdle);
    RENDER3D::SCREENSPACE::ReleaseScreenSpaceRuntimeState();
    RENDER3D::TEMPORAL::TemporalViewLease temporalView{};
    if (RENDER3D::TEMPORAL::AcquireTemporalView(RENDER3D::kPrimaryRenderViewId, temporalView)) {
        (void)RENDER3D::TEMPORAL::ResetTemporalFrameHistory(
            temporalView, RENDER3D::TEMPORAL::TemporalHistoryResetReason::ExplicitReset);
    }
    UpdateGpuContexts();

    gAppliedRenderQuality = effective;
    ++gRenderQualityTransitionCount;
    HIKARI_LOG_INFO(DescribeRenderQualityTransition(previous, effective, changes));
    return true;
}

bool RecoverLastEditorPresentation(bool editorViewport, const char* failedStage) {
    AbortActivePrimaryTemporalFrame();
    if (!editorViewport || !POST::PostSystem::IsEditorViewportReady()) {
        return false;
    }
    const uint64_t frameIndex = TIME::GetFrameContext().frameIndex;
    static uint64_t lastReportedFrame = 0u;
    if (lastReportedFrame == 0u || frameIndex - lastReportedFrame >= 120u) {
        lastReportedFrame = frameIndex;
        HIKARI_LOG_WARN(std::string("[RenderQuality] retained the last completed Game View after ") +
                        (failedStage != nullptr ? failedStage : "a frame-presentation failure") + ".");
    }
    gPresentationPrepared = true;
    return true;
}
} // namespace

void detail::SubmitPrimaryViewCameraFrame(const RENDER3D::ResolvedCameraFrame& cameraFrame,
                                          uint64_t sourceCameraObjectHandle, GameProjectPrimaryViewSource source,
                                          uint64_t worldIdentity, uint64_t frameIndex) noexcept {
    if (!cameraFrame.valid || worldIdentity == 0u || frameIndex != TIME::GetFrameContext().frameIndex) {
        return;
    }
    switch (source) {
    case GameProjectPrimaryViewSource::Game:
    case GameProjectPrimaryViewSource::EditorScene:
    case GameProjectPrimaryViewSource::EditorCameraPreview:
    case GameProjectPrimaryViewSource::EditorSequencePreview: break;
    default: return;
    }
    gPendingPrimaryCameraFrame.frame = cameraFrame;
    gPendingPrimaryCameraFrame.sourceCameraObjectHandle = sourceCameraObjectHandle;
    gPendingPrimaryCameraFrame.frameIndex = frameIndex;
    gPendingPrimaryCameraFrame.worldIdentity = worldIdentity;
    gPendingPrimaryCameraFrame.source = source;
    gPendingPrimaryCameraFrame.valid = true;
}

bool detail::TryGetLatestPrimaryViewSnapshot(uint64_t worldIdentity,
                                             GameProjectCameraViewSnapshot& outSnapshot) noexcept {
    if (worldIdentity == 0u || !gHasLatestPrimaryViewSnapshot ||
        gLatestPrimaryViewSnapshot.worldIdentity != worldIdentity) {
        return false;
    }
    outSnapshot = gLatestPrimaryViewSnapshot;
    return true;
}

void SetEditorGameViewportSize(int width, int height, bool visible) {
    gEditorGameViewportVisible = visible && width > 0 && height > 0;
    gEditorGameViewportWidth = gEditorGameViewportVisible ? width : 0;
    gEditorGameViewportHeight = gEditorGameViewportVisible ? height : 0;
}

bool IsEditorGameViewportVisible() noexcept { return gEditorGameViewportVisible; }

void UpdateGpuContexts() {
    GFX::PublishCurrentContext(gGraphicsCore.BuildContext());
    const GFX::Context& context = GFX::GetCurrentContext();
    RENDER3D::UPSCALING::UpdateStreamlineContext(context);
    DXTEX::DxTextureManager::UpdateContext(context);
    RENDER3D::UpdateRenderResourceDescriptorPoolContext(context);
    RENDER3D::UpdateClusterGeometryResourceContext(context);
    DX::DxRenderer::UpdateContext(context);
    POST::PostSystem::UpdateContext(context);
}

void QueueWindowResize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }
    gPendingWindowWidth = width;
    gPendingWindowHeight = height;
    gHasPendingWindowResize = true;
}

void ClearPendingWindowResize() {
    gHasPendingWindowResize = false;
    gPendingWindowWidth = 0;
    gPendingWindowHeight = 0;
}

bool ApplyPendingWindowResize() {
    if (!gHasPendingWindowResize) {
        return true;
    }

    const int width = gPendingWindowWidth;
    const int height = gPendingWindowHeight;

    if (width <= 0 || height <= 0) {
        ClearPendingWindowResize();
        return true;
    }
    if (width == static_cast<int>(GFX::GetCurrentContext().backBufferWidth) &&
        height == static_cast<int>(GFX::GetCurrentContext().backBufferHeight)) {
        gLogicalScreenWidth = width;
        gLogicalScreenHeight = height;
        HIKARI::CAMERA::SetScreenSize(width, height);
        ClearPendingWindowResize();
        return true;
    }

    const bool deactivateFrameGeneration = IsInProcessGamePresentationActive() &&
                                           gGamePresentationController.IsGameSurfaceActive() &&
                                           RENDER3D::UPSCALING::IsStreamlineFrameGenerationFeatureLoaded();
    if (deactivateFrameGeneration) {
        if (!gGraphicsCore.WaitForIdle()) {
            HIKARI_LOG_ERROR("Could not drain presentation work before DLSS-G resize suspension.");
            return false;
        }
        if (!RENDER3D::UPSCALING::DeactivateStreamlineFrameGeneration(true)) {
            HIKARI_LOG_ERROR("DLSS-G could not be deactivated before presentation resize.");
            return false;
        }
    }
    const bool resized = gGraphicsCore.Resize(width, height);
    if (!resized) {
        gGpuFrameReady = false;
        HIKARI_LOG_ERROR("D3D12 deferred resize failed; GPU frame recording disabled.");
        return false;
    }

    ClearPendingWindowResize();
    UpdateGpuContexts();
    gLogicalScreenWidth = width;
    gLogicalScreenHeight = height;
    HIKARI_LOG_INFO(std::string("Presentation resized to ") + std::to_string(width) + "x" + std::to_string(height) +
                    ".");
    HIKARI::CAMERA::SetScreenSize(gLogicalScreenWidth, gLogicalScreenHeight);
    HIKARI::CAMERA::SetScreenCenter({0.0f, 0.0f});
    return true;
}

bool IsRenderQualityApplied() {
    return gRenderQualityRuntimeInitialized && gAppliedRenderQuality == RENDER3D::GetRenderQualitySettings();
}

bool QueueRenderQualitySettings(const RENDER3D::RenderQualitySettings& settings) {
    if (!IsGamePresentationActive())
        return false;
    auto& state = INTERNAL::GetRuntimeServiceState();
    state.requestedRenderQuality = settings;
    state.hasRequestedRenderQuality = true;
    return true;
}

bool ApplyWindowPresentationSettings() {
    auto* targetWindow = IsInProcessGamePresentationActive() ? gGamePresentationController.GetGameWindow() : &gWindow;
    if (targetWindow == nullptr)
        return false;
    const RENDER3D::RenderQualitySettings& settings = RENDER3D::GetRenderQualitySettings();
    const RENDER3D::RenderResolution size = RENDER3D::ResolveFixedRenderResolution(settings.windowSize);
    const int windowWidth = size.width > 0 ? size.width : targetWindow->Width();
    const int windowHeight = size.height > 0 ? size.height : targetWindow->Height();

    PLATFORM::WindowMode mode = PLATFORM::WindowMode::Windowed;
    switch (settings.windowMode) {
    case RENDER3D::WindowPresentationMode::BorderlessWindow: mode = PLATFORM::WindowMode::BorderlessWindow; break;
    case RENDER3D::WindowPresentationMode::Fullscreen: mode = PLATFORM::WindowMode::Fullscreen; break;
    case RENDER3D::WindowPresentationMode::Windowed:
    default: mode = PLATFORM::WindowMode::Windowed; break;
    }

    return targetWindow->ApplyWindowMode(mode, windowWidth, windowHeight);
}

FrameSceneResolutionPlan ResolveFrameSceneResolutionPlan() {
    const RENDER3D::RenderQualitySettings& settings = RENDER3D::GetRenderQualitySettings();

    int viewportWidth = static_cast<int>(GFX::GetCurrentContext().backBufferWidth);
    int viewportHeight = static_cast<int>(GFX::GetCurrentContext().backBufferHeight);
    if (viewportWidth <= 0 || viewportHeight <= 0) {
        viewportWidth = gWindow.Width();
        viewportHeight = gWindow.Height();
    }
    if (ShouldProduceEditorUiFrame() && gEditorGameViewportVisible && gEditorGameViewportWidth > 0 &&
        gEditorGameViewportHeight > 0) {
        viewportWidth = gEditorGameViewportWidth;
        viewportHeight = gEditorGameViewportHeight;
    }

    FrameSceneResolutionPlan plan{};
    plan.output = RENDER3D::ResolveSceneOutputResolution(settings, viewportWidth, viewportHeight);
    plan.render = plan.output;
    plan.streamlineMode = RENDER3D::UPSCALING::ResolveStreamlineDlssMode(settings);

    if (RENDER3D::UPSCALING::IsStreamlineDlssSuperResolutionMode(plan.streamlineMode)) {
        RENDER3D::UPSCALING::StreamlineOptimalSettings optimal{};
        if (RENDER3D::UPSCALING::QueryStreamlineDlssOptimalSettings(
                plan.streamlineMode, static_cast<uint32_t>(plan.output.width),
                static_cast<uint32_t>(plan.output.height), optimal)) {
            plan.render.width = static_cast<int>(optimal.optimalRenderWidth);
            plan.render.height = static_cast<int>(optimal.optimalRenderHeight);
        }
    }
    return plan;
}

void ApplyFrameSceneCaptureSize() {
    const FrameSceneResolutionPlan plan = ResolveFrameSceneResolutionPlan();
    HIKARI::POST::PostSystem::SetSceneCaptureSize(plan.render.width, plan.render.height, plan.output.width,
                                                  plan.output.height);
    HIKARI::DX::DxRenderer::SetScreenSize(plan.render.width, plan.render.height);
}

bool BeginFrame(const RUNTIME::HostBootstrapConfig& cfg) {
    (void)cfg;
    gGpuFrameReady = false;
    gPresentationPrepared = false;
    gSceneLayersRendered = false;
    GFX::PIX::ScopedCpuEvent pixCpuFrame(GFX::PIX::kColorFrame, "Services.BeginFrame");
    gActionTimeDilationService.ApplyPending();
    const FrameContext& frame = HIKARI::TIME::BeginFrame();
    gActionTimeDilationService.Advance(frame.unscaledDt);
    CPU_PROFILE::BeginFrame(frame.frameIndex, frame.rawDt);
    CPU_PROFILE::ScopedCpuTimer cpuBeginFrame(CPU_PROFILE::Pass::ServicesBeginFrame);
    {
        static const auto qualityPass = CPU_PROFILE::RegisterDynamicPass("Frame.QualityAndResize");
        CPU_PROFILE::ScopedDynamicCpuTimer qualityTimer(qualityPass);
        if (!SynchronizeRenderQualityRuntime()) {
            return false;
        }
        if (!ApplyPendingWindowResize()) {
            return false;
        }
    }
    {
        static const auto resourcePass = CPU_PROFILE::RegisterDynamicPass("Frame.ResourceContexts");
        CPU_PROFILE::ScopedDynamicCpuTimer resourceTimer(resourcePass);
        UpdateGpuContexts();
    }

    {
        static const auto streamlinePass = CPU_PROFILE::RegisterDynamicPass("Frame.StreamlineToken");
        CPU_PROFILE::ScopedDynamicCpuTimer streamlineTimer(streamlinePass);
        (void)RENDER3D::UPSCALING::BeginStreamlineFrame(frame.frameIndex);
    }
    (void)RENDER3D::UPSCALING::BeginStreamlineReflexFrame();

    {
        static const auto graphicsPass = CPU_PROFILE::RegisterDynamicPass("Frame.GraphicsBegin");
        CPU_PROFILE::ScopedDynamicCpuTimer graphicsTimer(graphicsPass);
        if (!gGraphicsCore.BeginFrame(0.05f, 0.08f, 0.12f, 1.0f, frame.frameIndex)) {
            HIKARI_LOG_ERROR("D3D12 BeginFrame failed; skipping frame.");
            return false;
        }
    }
    gGpuFrameReady = true;

    {
        static const auto targetsPass = CPU_PROFILE::RegisterDynamicPass("Frame.SceneTargets");
        CPU_PROFILE::ScopedDynamicCpuTimer targetsTimer(targetsPass);
        ApplyFrameSceneCaptureSize();
        HIKARI::POST::PostSystem::UpdateCommonParams(frame.gameDt);
        const RENDER3D::RenderFrameToken renderFrame =
            RENDER3D::AllocateRenderFrameToken(frame.frameIndex, RENDER3D::kPrimaryRenderViewId);
        if (!HIKARI::POST::PostSystem::BeginSceneCapture(renderFrame, GFX::GetCurrentContext().deviceGeneration)) {
            HIKARI_LOG_ERROR("Scene capture surface frame initialization failed.");
            CloseFailedBeginFrame();
            return false;
        }
    }

    {
        static const auto uiPass = CPU_PROFILE::RegisterDynamicPass("Frame.UiSources");
        CPU_PROFILE::ScopedDynamicCpuTimer uiTimer(uiPass);
        HIKARI::RENDERER::BeginFrame();
        HIKARI::GAME_UI::BeginGameUiFrame();
        DX::DxRenderer::BeginFrame();
    }

    {
        static const auto inputPass = CPU_PROFILE::RegisterDynamicPass("Frame.Input");
        CPU_PROFILE::ScopedDynamicCpuTimer inputTimer(inputPass);
        PLATFORM::Win32Window* gameWindow = gGamePresentationController.GetGameWindow();
        PLATFORM::Win32Window* inputWindow = nullptr;
        if (gameWindow != nullptr && gameWindow->GetHWND() == gInputService.GetHostWindow()) {
            inputWindow = gameWindow;
        } else if (gWindow.GetHWND() == gInputService.GetHostWindow()) {
            inputWindow = &gWindow;
        }
        if (inputWindow != &gWindow) {
            gWindow.DiscardTextInputEvents();
        }
        if (gameWindow != nullptr && inputWindow != gameWindow) {
            gameWindow->DiscardTextInputEvents();
        }
        gInputService.SetExternalMouseWheel(inputWindow != nullptr ? inputWindow->ConsumeMouseWheelDelta()
                                                                   : gWindow.ConsumeMouseWheelDelta());
        if (inputWindow != nullptr) {
            gInputService.SetExternalTextInputEvents(inputWindow->ConsumeTextInputEvents());
        } else {
            gInputService.SetExternalTextInputEvents({});
        }
        gInputService.SetSimulationTick(frame.fixedTickIndex);
        gInputService.Update(frame.unscaledDt);
    }
    if (RUNTIME::IEditorUiServices* editorUiServices = RUNTIME::GetEditorUiServices()) {
        static const auto editorUiPass = CPU_PROFILE::RegisterDynamicPass("Frame.EditorUiBegin");
        CPU_PROFILE::ScopedDynamicCpuTimer editorUiTimer(editorUiPass);
        editorUiServices->BeginFrame(gWindow, GFX::GetCurrentContext(), frame.unscaledDt, ShouldProduceHostUiFrame());
    }
    HIKARI::CAMERA::Update(frame.gameDt, &gInputService.GetSnapshot());
    return true;
}

bool PrepareFramePresentation(FramePresentationDestination destination) {
    if (!gGpuFrameReady) {
        return false;
    }
    if (gPresentationPrepared) {
        return true;
    }

    const bool editorViewport =
        destination == FramePresentationDestination::EditorViewport && IsEditorHost() && IsEditorUIEnabled();

    int renderWidth = 0;
    int renderHeight = 0;
    HIKARI::POST::PostSystem::GetSceneCaptureSize(renderWidth, renderHeight);
    // 場面層の 2D は表示参照の色。主ビューの露出空間（P/E）で HDR 目標へ書く。
    const float sceneLayerColorScale =
        RENDER3D::COLOR::MakeViewExposureConstants(HIKARI::POST::PostSystem::GetExposureState()).z;
    if (!HIKARI::DX::DxRenderer::SetOutputTarget(DXGI_FORMAT_R16G16B16A16_FLOAT, renderWidth, renderHeight,
                                                 sceneLayerColorScale)) {
        HIKARI_LOG_ERROR("Render2D HDR output target is unavailable.");
        return RecoverLastEditorPresentation(editorViewport, "Render2D HDR target preparation failed");
    }

    if (!gSceneLayersRendered) {
        GFX::PIX::ScopedGpuEvent pixScene(GFX::GetCurrentContext().cmdList, GFX::PIX::kColorRender, "Scene Layers");
        GFX::GPU_PROFILE::ScopedGpuTimer gpuScene(GFX::GetCurrentContext().cmdList,
                                                  GFX::GPU_PROFILE::Pass::SceneLayers);
        HIKARI::RENDERER::RenderLayerRange(HIKARI::RENDERER::RenderLayer::Background,
                                           HIKARI::RENDERER::RenderLayer::VFX, false);
        gSceneLayersRendered = true;
    }
    if (!HIKARI::POST::PostSystem::ContinueSceneCaptureGraphSession()) {
        HIKARI_LOG_ERROR("Render frame graph presentation continuation failed.");
        return RecoverLastEditorPresentation(editorViewport, "render-graph continuation failed");
    }
    {
        CPU_PROFILE::ScopedCpuTimer cpuPost(CPU_PROFILE::Pass::PresentationComposition);
        GFX::PIX::ScopedGpuEvent pixPost(GFX::GetCurrentContext().cmdList, GFX::PIX::kColorPost, "PostSystem");
        GFX::GPU_PROFILE::ScopedGpuTimer gpuPost(GFX::GetCurrentContext().cmdList,
                                                 GFX::GPU_PROFILE::Pass::PresentationComposition);
        if (!HIKARI::POST::PostSystem::PrepareGameUiComposition(
                editorViewport,
                editorViewport && gEditorGameViewportWidth > 0 ? gEditorGameViewportWidth : gLogicalScreenWidth,
                editorViewport && gEditorGameViewportHeight > 0 ? gEditorGameViewportHeight : gLogicalScreenHeight)) {
            if (RecoverLastEditorPresentation(editorViewport, "Game UI composition preparation failed")) {
                return true;
            }
            HIKARI_LOG_ERROR("Game UI composition preparation failed.");
            return false;
        }
    }

    {
        GFX::PIX::ScopedGpuEvent pixUi(GFX::GetCurrentContext().cmdList, GFX::PIX::kColorEditor,
                                       "Game UI and Debug Layers");
        // UI 層とデバッグ層は Render2D、ゲーム UI の効果描画は Render3D/Ui が持つ。計時もその持ち主ごとに分ける。
        const auto renderLayer = [](HIKARI::RENDERER::RenderLayer layer, bool debug) {
            CPU_PROFILE::ScopedCpuTimer cpuLayer(CPU_PROFILE::Pass::UiLayers);
            GFX::GPU_PROFILE::ScopedGpuTimer gpuLayer(GFX::GetCurrentContext().cmdList,
                                                      GFX::GPU_PROFILE::Pass::UiLayers);
            HIKARI::RENDERER::RenderLayerRange(layer, layer, debug);
        };
        renderLayer(HIKARI::RENDERER::RenderLayer::UI, false);
        {
            CPU_PROFILE::ScopedCpuTimer cpuGameUi(CPU_PROFILE::Pass::GameUi);
            GFX::GPU_PROFILE::ScopedGpuTimer gpuGameUi(GFX::GetCurrentContext().cmdList,
                                                       GFX::GPU_PROFILE::Pass::GameUi);
            const HIKARI::POST::PresentationFrameResources& presentation =
                HIKARI::POST::PostSystem::GetPresentationFrameResources();
            if (!HIKARI::GAME_UI::RenderGameUiFrame(HIKARI::TIME::GetFrameContext().frameIndex,
                                                    presentation.uiLogicalWidth, presentation.uiLogicalHeight,
                                                    presentation.width, presentation.height)) {
                HIKARI_LOG_ERROR("Game project UI effect rendering failed.");
            }
        }
        renderLayer(HIKARI::RENDERER::RenderLayer::Debug, true);
    }

    if (!HIKARI::POST::PostSystem::FinalizeGameUiComposition(editorViewport)) {
        HIKARI_LOG_ERROR("Game UI composition finalization failed.");
        return RecoverLastEditorPresentation(editorViewport, "Game UI composition finalization failed");
    }

    const HIKARI::POST::PresentationFrameResources& presentation =
        HIKARI::POST::PostSystem::GetPresentationFrameResources();
    RENDER3D::UPSCALING::SynchronizeStreamlineFrameGenerationPolicy(RENDER3D::GetRenderQualitySettings());
    if (presentation.HasFrameGenerationInputs()) {
        RENDER3D::TEMPORAL::TemporalInputs temporal{};
        if (HIKARI::POST::PostSystem::TryGetPresentationTemporalInputs(temporal)) {
            (void)RENDER3D::UPSCALING::SubmitStreamlineFrameGenerationInputs(
                temporal, *presentation.hudlessColor, *presentation.uiColorAndAlpha, presentation.frameIndex,
                IsGamePresentationActive(), presentation.sceneRect);
        }
    }

    if (!HIKARI::POST::PostSystem::FinishPresentationTemporalFrame(true)) {
        HIKARI_LOG_ERROR("Temporal presentation resource frame did not close.");
        return RecoverLastEditorPresentation(editorViewport, "temporal presentation close failed");
    }
    gPreparedPresentationDestination =
        editorViewport ? FramePresentationDestination::EditorViewport : FramePresentationDestination::BackBuffer;
    gPresentationPrepared = true;
    return true;
}

bool RequestFrameCapture(const std::filesystem::path& output) { return gGraphicsCore.RequestFrameCapture(output); }

bool EndFrame() {
    if (!gGpuFrameReady) {
        return false;
    }
    GFX::PIX::ScopedCpuEvent pixCpuFrame(GFX::PIX::kColorFrame, "Services.EndFrame");
    if (!PrepareFramePresentation()) {
        HIKARI::RENDERER::ClearSubmittedCommands();
    }
    if (RUNTIME::IEditorUiServices* editorUiServices = RUNTIME::GetEditorUiServices()) {
        editorUiServices->RenderFrame(GFX::GetCurrentContext(), ShouldProduceHostUiFrame());
    }

    gGraphicsCore.SetVSyncEnabled(RENDER3D::GetRenderQualitySettings().vSync);
    if (!gGraphicsCore.EndFrame()) {
        gGpuFrameReady = false;
        HIKARI_LOG_ERROR("D3D12 EndFrame failed; stopping GPU frame loop.");
        CPU_PROFILE::EndFrame();
        return false;
    }
    if (gPresentationPrepared) {
        const POST::PresentationFrameResources& presentation = POST::PostSystem::GetPresentationFrameResources();
        GameProjectCameraViewSnapshot snapshot{};
        const GameProjectPresentationTarget target =
            gPreparedPresentationDestination == FramePresentationDestination::EditorViewport
                ? GameProjectPresentationTarget::EditorViewport
                : GameProjectPresentationTarget::BackBuffer;
        if (BuildPresentedViewSnapshot(presentation, target, snapshot)) {
            gLatestPrimaryViewSnapshot = snapshot;
            gHasLatestPrimaryViewSnapshot = true;
        }
    }
    if (!RENDER3D::UPSCALING::CaptureStreamlineFrameGenerationCompletionAfterPresent()) {
        HIKARI_LOG_WARN(
            "[Streamline] Could not capture DLSS-G completion state after Present; resource release will remain guarded.");
    }
    UpdateGamePresentationPerformanceTitle();
    gGpuFrameReady = false;
    GFX::PIX::Update();
    CPU_PROFILE::EndFrame();
    return true;
}
} // namespace SERVICES
} // namespace HIKARI
