#include "Runtime/Presentation/HIKARI_GamePresentationService.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Runtime/Services/Internal/HIKARI_RuntimeServiceState.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>

#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Render2D/HIKARI_Camera.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"

namespace HIKARI {
    namespace SERVICES {
        namespace {
            auto& gGraphicsCore = INTERNAL::GetRuntimeServiceState().core;
            auto& gWindow = INTERNAL::GetRuntimeServiceState().window;
            auto& gGamePresentationController =
                INTERNAL::GetRuntimeServiceState().gamePresentationController;
            auto& gInputService =
                INTERNAL::GetRuntimeServiceState().inputService;
            auto& gRuntimeHostMode =
                INTERNAL::GetRuntimeServiceState().runtimeHostMode;
            auto& gGpuFrameReady =
                INTERNAL::GetRuntimeServiceState().gpuFrameReady;
            auto& gHasPendingWindowResize =
                INTERNAL::GetRuntimeServiceState().hasPendingWindowResize;
            auto& gPendingWindowWidth =
                INTERNAL::GetRuntimeServiceState().pendingWindowWidth;
            auto& gPendingWindowHeight =
                INTERNAL::GetRuntimeServiceState().pendingWindowHeight;
            auto& gLogicalScreenWidth =
                INTERNAL::GetRuntimeServiceState().logicalScreenWidth;
            auto& gLogicalScreenHeight =
                INTERNAL::GetRuntimeServiceState().logicalScreenHeight;
            auto& gRuntimeWindowBaseTitle =
                INTERNAL::GetRuntimeServiceState().runtimeWindowBaseTitle;
            auto& gPreviewTelemetrySeconds =
                INTERNAL::GetRuntimeServiceState().previewTelemetrySeconds;
            auto& gPreviewTelemetryRenderFrames =
                INTERNAL::GetRuntimeServiceState().previewTelemetryRenderFrames;
            auto& gPreviewTelemetryPresentedFrames =
                INTERNAL::GetRuntimeServiceState().previewTelemetryPresentedFrames;
        } // namespace

        bool IsInProcessGamePresentationActive() {
            return gGamePresentationController.IsActive();
        }

        bool IsGamePresentationActive() {
            return IsStandaloneGameHost() || IsInProcessGamePresentationActive();
        }

        bool BeginInProcessGamePresentation() {
            if (!IsEditorHost() ||
                gGpuFrameReady ||
                IsInProcessGamePresentationActive()) {
                return false;
            }

            const RENDER3D::RenderQualitySettings& settings =
                RENDER3D::GetRenderQualitySettings();
            RENDER3D::RenderResolution size =
                RENDER3D::ResolveFixedRenderResolution(settings.windowSize);
            if (size.width <= 0 || size.height <= 0) {
                size = { 1280, 720 };
            }
            if (!gGraphicsCore.WaitForIdle()) {
                HIKARI_LOG_ERROR(
                    "Could not flush GPU work before entering game presentation.");
                return false;
            }

            RUNTIME::GamePresentationConfig config{};
            config.title = L"HIKARI Game Preview";
            config.width = size.width;
            config.height = size.height;
            config.resizable =
                settings.windowMode ==
                RENDER3D::WindowPresentationMode::Windowed;
            switch (settings.windowMode) {
            case RENDER3D::WindowPresentationMode::BorderlessWindow:
                config.windowMode = PLATFORM::WindowMode::BorderlessWindow;
                break;
            case RENDER3D::WindowPresentationMode::Fullscreen:
                config.windowMode = PLATFORM::WindowMode::Fullscreen;
                break;
            case RENDER3D::WindowPresentationMode::Windowed:
            default:
                config.windowMode = PLATFORM::WindowMode::Windowed;
                break;
            }

            const bool wantsFrameGeneration =
                settings.frameGenerationMode ==
                RENDER3D::RenderFrameGenerationMode::Dlss;
            bool frameGenerationLoaded = false;
            (void)RENDER3D::UPSCALING::
                DeactivateStreamlineFrameGeneration(true);
            if (wantsFrameGeneration) {
                frameGenerationLoaded =
                    RENDER3D::UPSCALING::SetStreamlineFrameGenerationFeatureLoaded(true);
                if (!frameGenerationLoaded) {
                    HIKARI_LOG_WARN(
                        "[Streamline] game preview will continue without DLSS-G because the feature could not be loaded.");
                }
            }
            if (!gGamePresentationController.Begin(
                    gGraphicsCore,
                    gWindow,
                    config,
                    [](int width, int height) {
                        QueueWindowResize(width, height);
                    })) {
                if (frameGenerationLoaded) {
                    (void)RENDER3D::UPSCALING::SetStreamlineFrameGenerationFeatureLoaded(false);
                }
                return false;
            }

            gHasPendingWindowResize = false;
            gPendingWindowWidth = 0;
            gPendingWindowHeight = 0;
            UpdateGpuContexts();
            PLATFORM::Win32Window* gameWindow =
                gGamePresentationController.GetGameWindow();
            if (gameWindow == nullptr) {
                (void)RENDER3D::UPSCALING::
                    DeactivateStreamlineFrameGeneration(true);
                if (gGamePresentationController.IsGameSurfaceActive()) {
                    (void)gGamePresentationController.ReleaseGameSurface(gGraphicsCore);
                }
                (void)RENDER3D::UPSCALING::
                    SetStreamlineFrameGenerationFeatureLoaded(false);
                (void)gGamePresentationController.RestoreEditorSurface(
                    gGraphicsCore, gWindow);
                UpdateGpuContexts();
                return false;
            }
            gInputService.SetHostWindow(gameWindow->GetHWND());
            gInputService.ClearMouseCaptureRegion();
            gInputService.SetMouseCaptureMode(
                INPUT::MouseCaptureMode::Relative);
            gLogicalScreenWidth = gameWindow->Width();
            gLogicalScreenHeight = gameWindow->Height();
            HIKARI::CAMERA::SetScreenSize(gLogicalScreenWidth, gLogicalScreenHeight);
            HIKARI::CAMERA::SetScreenCenter({ 0.0f, 0.0f });
            gPreviewTelemetrySeconds = 0.0;
            gPreviewTelemetryRenderFrames = 0;
            gPreviewTelemetryPresentedFrames = 0;
            RENDER3D::TEMPORAL::ResetTemporalFrameHistory(
                RENDER3D::TEMPORAL::TemporalHistoryResetReason::ExplicitReset);
            HIKARI_LOG_INFO("In-process game presentation started.");
            return true;
        }

        bool BeginInProcessGamePresentationStop() {
            if (!IsInProcessGamePresentationActive()) {
                return true;
            }
            if (gGpuFrameReady) {
                return false;
            }
            gGamePresentationController.MarkStopping();
            gInputService.SetMouseCaptureMode(
                INPUT::MouseCaptureMode::Free);
            if (!gGraphicsCore.WaitForIdle()) {
                gInputService.SetMouseCaptureMode(
                    INPUT::MouseCaptureMode::Relative);
                HIKARI_LOG_ERROR(
                    "Could not drain the presenting queue before stopping DLSS-G.");
                return false;
            }
            if (!RENDER3D::UPSCALING::
                    DeactivateStreamlineFrameGeneration(true)) {
                gInputService.SetMouseCaptureMode(
                    INPUT::MouseCaptureMode::Relative);
                HIKARI_LOG_ERROR("Could not deactivate DLSS-G before stopping Play.");
                return false;
            }
            HIKARI_LOG_INFO("In-process game presentation stop requested.");
            return true;
        }

        bool DrainInProcessGamePresentation() {
            if (!IsInProcessGamePresentationActive() ||
                gGamePresentationController.IsGameSurfaceReleased()) {
                return true;
            }
            if (gGpuFrameReady || !gGraphicsCore.WaitForIdle()) {
                HIKARI_LOG_ERROR(
                    "Could not drain GPU work before releasing the game presentation surface.");
                return false;
            }
            return gGamePresentationController.ReleaseGameSurface(gGraphicsCore);
        }

        bool RestoreEditorPresentation() {
            if (!IsInProcessGamePresentationActive()) {
                return true;
            }
            if (!gGamePresentationController.IsGameSurfaceReleased()) {
                return false;
            }
            const bool featureUnloaded =
                !RENDER3D::UPSCALING::
                    IsStreamlineFrameGenerationFeatureLoaded() ||
                RENDER3D::UPSCALING::
                    SetStreamlineFrameGenerationFeatureLoaded(false);
            const bool editorRestored =
                gGamePresentationController.RestoreEditorSurface(
                    gGraphicsCore, gWindow);
            if (!editorRestored) {
                return false;
            }
            gHasPendingWindowResize = false;
            gPendingWindowWidth = 0;
            gPendingWindowHeight = 0;
            UpdateGpuContexts();
            gInputService.SetMouseCaptureMode(
                INPUT::MouseCaptureMode::Free);
            gInputService.ClearMouseCaptureRegion();
            gInputService.SetHostWindow(gWindow.GetHWND());
            gLogicalScreenWidth = gWindow.Width();
            gLogicalScreenHeight = gWindow.Height();
            HIKARI::CAMERA::SetScreenSize(gLogicalScreenWidth, gLogicalScreenHeight);
            HIKARI::CAMERA::SetScreenCenter({ 0.0f, 0.0f });
            gPreviewTelemetrySeconds = 0.0;
            gPreviewTelemetryRenderFrames = 0;
            gPreviewTelemetryPresentedFrames = 0;
            RENDER3D::TEMPORAL::ResetTemporalFrameHistory(
                RENDER3D::TEMPORAL::TemporalHistoryResetReason::ExplicitReset);
            HIKARI_LOG_INFO("In-process game presentation stopped.");
            if (!featureUnloaded) {
                HIKARI_LOG_ERROR(
                    "Editor presentation recovered, but DLSS-G could not be unloaded. Frame generation is unavailable until restart.");
            }
            return featureUnloaded;
        }

        bool EndInProcessGamePresentation() {
            if (!BeginInProcessGamePresentationStop()) {
                return false;
            }
            if (!DrainInProcessGamePresentation()) {
                return false;
            }
            return RestoreEditorPresentation();
        }

        bool ConsumeInProcessGameCloseRequest() {
            return gGamePresentationController.HasCloseRequest();
        }

        void UpdateGamePresentationPerformanceTitle() {
            PLATFORM::Win32Window* presentationWindow = nullptr;
            const wchar_t* baseTitle = nullptr;
            if (gRuntimeHostMode == RuntimeHostMode::GamePreview) {
                presentationWindow = &gWindow;
                baseTitle = gRuntimeWindowBaseTitle.c_str();
            }
            else if (IsInProcessGamePresentationActive()) {
                presentationWindow = gGamePresentationController.GetGameWindow();
                baseTitle = L"HIKARI Game Preview";
            }
            if (presentationWindow == nullptr ||
                presentationWindow->GetHWND() == nullptr) {
                return;
            }

            const FrameContext& frame = TIME::GetFrameContext();
            gPreviewTelemetrySeconds += (std::max)(0.0f, frame.rawDt);
            ++gPreviewTelemetryRenderFrames;

            const auto& frameGeneration =
                RENDER3D::UPSCALING::GetStreamlineFrameGenerationStats();
            const bool frameGenerationActive =
                frameGeneration.status ==
                    RENDER3D::UPSCALING::StreamlineFrameGenerationStatus::Active &&
                frameGeneration.tagsSubmitted;
            gPreviewTelemetryPresentedFrames += frameGenerationActive
                ? (std::max)(1u, frameGeneration.presentedFrames)
                : 1u;

            constexpr double kTelemetryIntervalSeconds = 0.5;
            if (gPreviewTelemetrySeconds < kTelemetryIntervalSeconds) {
                return;
            }

            const double renderFps =
                static_cast<double>(gPreviewTelemetryRenderFrames) /
                gPreviewTelemetrySeconds;
            const double displayFps =
                static_cast<double>(gPreviewTelemetryPresentedFrames) /
                gPreviewTelemetrySeconds;
            const double actualMultiplier =
                gPreviewTelemetryRenderFrames > 0
                ? static_cast<double>(gPreviewTelemetryPresentedFrames) /
                    static_cast<double>(gPreviewTelemetryRenderFrames)
                : 1.0;

            std::wostringstream title;
            title << baseTitle
                << L" | Render " << std::fixed << std::setprecision(1)
                << renderFps << L" FPS"
                << L" | Display " << displayFps << L" FPS";
            if (frameGeneration.requested) {
                title << L" | DLSS-G ";
                if (frameGenerationActive) {
                    title << std::setprecision(2) << actualMultiplier << L"x";
                } else {
                    const auto& streamline =
                        RENDER3D::UPSCALING::GetStreamlineDebugStats();
                    if (frameGeneration.status ==
                            RENDER3D::UPSCALING::StreamlineFrameGenerationStatus::ResourcePressure ||
                        (frameGeneration.status ==
                            RENDER3D::UPSCALING::StreamlineFrameGenerationStatus::BlockedByUpscaler &&
                         streamline.status ==
                            RENDER3D::UPSCALING::StreamlineRuntimeStatus::ResourcePressure)) {
                        title << L"blocked: VRAM";
                    } else {
                        const char* status =
                            RENDER3D::UPSCALING::ToString(frameGeneration.status);
                        std::wstring wideStatus;
                        while (status != nullptr && *status != '\0') {
                            wideStatus.push_back(static_cast<wchar_t>(*status++));
                        }
                        title << wideStatus;
                    }
                }
            }
            (void)presentationWindow->SetTitle(title.str().c_str());

            gPreviewTelemetrySeconds = 0.0;
            gPreviewTelemetryRenderFrames = 0;
            gPreviewTelemetryPresentedFrames = 0;
        }
    } // namespace SERVICES
} // namespace HIKARI
