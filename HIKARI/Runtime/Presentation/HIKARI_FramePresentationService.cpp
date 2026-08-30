#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Runtime/Presentation/HIKARI_GamePresentationService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Runtime/Services/Internal/HIKARI_RuntimeServiceState.h"

#include <string>

#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render2D/HIKARI_Camera.h"
#include "Render2D/HIKARI_DxRenderer.h"
#include "Render2D/HIKARI_GameUiEffectRenderer.h"
#include "Render2D/HIKARI_DxTexture.h"
#include "Render2D/HIKARI_Renderer.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Core/HIKARI_RenderFrameToken.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGenerationPolicy.h"
#include "Render3D/Upscaling/HIKARI_StreamlineReflex.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Runtime/Host/HIKARI_HostBootstrapConfig.h"
#include "Runtime/Host/HIKARI_IEditorUiServices.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI {
    namespace SERVICES {
        namespace {
            auto& gGraphicsCore = INTERNAL::GetRuntimeServiceState().core;
            auto& gWindow = INTERNAL::GetRuntimeServiceState().window;
            auto& gInputService = INTERNAL::GetRuntimeServiceState().inputService;
            auto& gActionTimeDilationService =
                INTERNAL::GetRuntimeServiceState().actionTimeDilationService;
            auto& gGamePresentationController =
                INTERNAL::GetRuntimeServiceState().gamePresentationController;
            auto& gGpuFrameReady =
                INTERNAL::GetRuntimeServiceState().gpuFrameReady;
            auto& gPresentationPrepared =
                INTERNAL::GetRuntimeServiceState().presentationPrepared;
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
            auto& gEditorGameViewportVisible =
                INTERNAL::GetRuntimeServiceState().editorGameViewportVisible;
            auto& gEditorGameViewportWidth =
                INTERNAL::GetRuntimeServiceState().editorGameViewportWidth;
            auto& gEditorGameViewportHeight =
                INTERNAL::GetRuntimeServiceState().editorGameViewportHeight;

            void AbortActivePrimaryTemporalFrame() {
                POST::PostSystem::AbortSceneCaptureTemporalFrame(
                    RENDER3D::TEMPORAL::
                        TemporalHistoryResetReason::PreviousFrameIncomplete);
            }

            void CloseFailedBeginFrame() {
                HIKARI::RENDERER::ClearSubmittedCommands();
                AbortActivePrimaryTemporalFrame();
                RENDER3D::UPSCALING::EndStreamlineReflexSimulation();
                gGraphicsCore.SetVSyncEnabled(
                    RENDER3D::GetRenderQualitySettings().vSync);
                if (!gGraphicsCore.EndFrame()) {
                    HIKARI_LOG_ERROR(
                        "D3D12 failed to close an aborted begin-frame.");
                }
                (void)RENDER3D::UPSCALING::
                    CaptureStreamlineFrameGenerationCompletionAfterPresent();
                gGpuFrameReady = false;
                GFX::PIX::Update();
                CPU_PROFILE::EndFrame();
            }
        } // namespace

        void SetEditorGameViewportSize(int width, int height, bool visible) {
            gEditorGameViewportVisible = visible && width > 0 && height > 0;
            gEditorGameViewportWidth = gEditorGameViewportVisible ? width : 0;
            gEditorGameViewportHeight = gEditorGameViewportVisible ? height : 0;
        }

        bool IsEditorGameViewportVisible() noexcept {
            return gEditorGameViewportVisible;
        }

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
                ClearPendingWindowResize();
                return true;
            }

            const bool deactivateFrameGeneration =
                IsInProcessGamePresentationActive() &&
                gGamePresentationController.IsGameSurfaceActive() &&
                RENDER3D::UPSCALING::
                    IsStreamlineFrameGenerationFeatureLoaded();
            if (deactivateFrameGeneration) {
                if (!gGraphicsCore.WaitForIdle()) {
                    HIKARI_LOG_ERROR(
                        "Could not drain presentation work before DLSS-G resize suspension.");
                    return false;
                }
                if (!RENDER3D::UPSCALING::
                        DeactivateStreamlineFrameGeneration(true)) {
                    HIKARI_LOG_ERROR(
                        "DLSS-G could not be deactivated before presentation resize.");
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
            HIKARI_LOG_INFO(
                std::string("Presentation resized to ") +
                std::to_string(width) + "x" + std::to_string(height) + ".");
            HIKARI::CAMERA::SetScreenSize(gLogicalScreenWidth, gLogicalScreenHeight);
            HIKARI::CAMERA::SetScreenCenter({ 0.0f, 0.0f });
            return true;
        }

        bool ApplyWindowPresentationSettings() {
            const RENDER3D::RenderQualitySettings& settings =
                RENDER3D::GetRenderQualitySettings();
            const RENDER3D::RenderResolution size =
                RENDER3D::ResolveFixedRenderResolution(settings.windowSize);
            const int windowWidth = size.width > 0 ? size.width : gWindow.Width();
            const int windowHeight = size.height > 0 ? size.height : gWindow.Height();

            PLATFORM::WindowMode mode = PLATFORM::WindowMode::Windowed;
            switch (settings.windowMode) {
            case RENDER3D::WindowPresentationMode::BorderlessWindow:
                mode = PLATFORM::WindowMode::BorderlessWindow;
                break;
            case RENDER3D::WindowPresentationMode::Fullscreen:
                mode = PLATFORM::WindowMode::Fullscreen;
                break;
            case RENDER3D::WindowPresentationMode::Windowed:
            default:
                mode = PLATFORM::WindowMode::Windowed;
                break;
            }

            return gWindow.ApplyWindowMode(mode, windowWidth, windowHeight);
        }

        FrameSceneResolutionPlan ResolveFrameSceneResolutionPlan() {
            const RENDER3D::RenderQualitySettings& settings =
                RENDER3D::GetRenderQualitySettings();

            int viewportWidth = static_cast<int>(GFX::GetCurrentContext().backBufferWidth);
            int viewportHeight = static_cast<int>(GFX::GetCurrentContext().backBufferHeight);
            if (viewportWidth <= 0 || viewportHeight <= 0) {
                viewportWidth = gWindow.Width();
                viewportHeight = gWindow.Height();
            }
            if (ShouldProduceEditorUiFrame() &&
                gEditorGameViewportVisible &&
                gEditorGameViewportWidth > 0 &&
                gEditorGameViewportHeight > 0) {
                viewportWidth = gEditorGameViewportWidth;
                viewportHeight = gEditorGameViewportHeight;
            }

            FrameSceneResolutionPlan plan{};
            plan.output = RENDER3D::ResolveSceneOutputResolution(
                settings,
                viewportWidth,
                viewportHeight);
            plan.render = plan.output;
            plan.streamlineMode =
                RENDER3D::UPSCALING::ResolveStreamlineDlssMode(settings);

            if (RENDER3D::UPSCALING::IsStreamlineDlssSuperResolutionMode(
                    plan.streamlineMode)) {
                RENDER3D::UPSCALING::StreamlineOptimalSettings optimal{};
                if (RENDER3D::UPSCALING::QueryStreamlineDlssOptimalSettings(
                        plan.streamlineMode,
                        static_cast<uint32_t>(plan.output.width),
                        static_cast<uint32_t>(plan.output.height),
                        optimal)) {
                    plan.render.width =
                        static_cast<int>(optimal.optimalRenderWidth);
                    plan.render.height =
                        static_cast<int>(optimal.optimalRenderHeight);
                }
            }
            return plan;
        }

        void ApplyFrameSceneCaptureSize() {
            const FrameSceneResolutionPlan plan =
                ResolveFrameSceneResolutionPlan();
            HIKARI::POST::PostSystem::SetSceneCaptureSize(
                plan.render.width,
                plan.render.height,
                plan.output.width,
                plan.output.height);
            HIKARI::DX::DxRenderer::SetScreenSize(
                plan.render.width,
                plan.render.height);
        }

        bool BeginFrame(
            const RUNTIME::HostBootstrapConfig& cfg) {
            (void)cfg;
            gGpuFrameReady = false;
            gPresentationPrepared = false;
            GFX::PIX::ScopedCpuEvent pixCpuFrame(GFX::PIX::kColorFrame, "Services.BeginFrame");
            gActionTimeDilationService.ApplyPending();
            const FrameContext& frame = HIKARI::TIME::BeginFrame();
            gActionTimeDilationService.Advance(frame.unscaledDt);
            CPU_PROFILE::BeginFrame(frame.frameIndex);
            CPU_PROFILE::ScopedCpuTimer cpuBeginFrame(
                CPU_PROFILE::Pass::ServicesBeginFrame);
            if (!ApplyPendingWindowResize()) {
                return false;
            }
            UpdateGpuContexts();

            (void)RENDER3D::UPSCALING::BeginStreamlineFrame(frame.frameIndex);
            (void)RENDER3D::UPSCALING::BeginStreamlineReflexFrame();

            if (!gGraphicsCore.BeginFrame(0.05f, 0.08f, 0.12f, 1.0f)) {
                HIKARI_LOG_ERROR("D3D12 BeginFrame failed; skipping frame.");
                return false;
            }
            gGpuFrameReady = true;

            ApplyFrameSceneCaptureSize();
            HIKARI::POST::PostSystem::UpdateCommonParams(frame.gameDt);
            const RENDER3D::RenderFrameToken renderFrame =
                RENDER3D::AllocateRenderFrameToken(
                    frame.frameIndex,
                    RENDER3D::kPrimaryRenderViewId);
            if (!HIKARI::POST::PostSystem::BeginSceneCapture(
                    renderFrame,
                    GFX::GetCurrentContext().deviceGeneration)) {
                HIKARI_LOG_ERROR(
                    "Scene capture surface frame initialization failed.");
                CloseFailedBeginFrame();
                return false;
            }

            HIKARI::RENDERER::BeginFrame();
            HIKARI::GAME_UI::BeginGameUiFrame();

            DX::DxRenderer::BeginFrame();
            PLATFORM::Win32Window* inputWindow =
                gGamePresentationController.GetGameWindow();
            gInputService.SetExternalMouseWheel(
                inputWindow != nullptr
                    ? inputWindow->ConsumeMouseWheelDelta()
                    : gWindow.ConsumeMouseWheelDelta());
            gInputService.Update(frame.unscaledDt);
            if (RUNTIME::IEditorUiServices* editorUiServices =
                    RUNTIME::GetEditorUiServices()) {
                editorUiServices->BeginFrame(
                    gWindow,
                    GFX::GetCurrentContext(),
                    frame.unscaledDt,
                    ShouldProduceEditorUiFrame());
            }
            HIKARI::CAMERA::Update(
                frame.gameDt,
                &gInputService.GetSnapshot());
            return true;
        }

        bool PrepareFramePresentation(
            FramePresentationDestination destination) {
            if (!gGpuFrameReady) {
                return false;
            }
            if (gPresentationPrepared) {
                return true;
            }

            int renderWidth = 0;
            int renderHeight = 0;
            HIKARI::POST::PostSystem::GetSceneCaptureSize(
                renderWidth,
                renderHeight);
            if (!HIKARI::DX::DxRenderer::SetOutputTarget(
                    DXGI_FORMAT_R16G16B16A16_FLOAT,
                    renderWidth,
                    renderHeight)) {
                HIKARI_LOG_ERROR("Render2D HDR output target is unavailable.");
                AbortActivePrimaryTemporalFrame();
                return false;
            }

            {
                GFX::PIX::ScopedGpuEvent pixScene(
                    GFX::GetCurrentContext().cmdList,
                    GFX::PIX::kColorRender,
                    "Scene Layers");
                GFX::GPU_PROFILE::ScopedGpuTimer gpuScene(
                    GFX::GetCurrentContext().cmdList,
                    GFX::GPU_PROFILE::Pass::SceneLayers);
                HIKARI::RENDERER::RenderLayerRange(
                    HIKARI::RENDERER::RenderLayer::Background,
                    HIKARI::RENDERER::RenderLayer::VFX,
                    false);
            }

            const bool editorViewport =
                destination == FramePresentationDestination::EditorViewport &&
                IsEditorHost() &&
                IsEditorUIEnabled();
            {
                CPU_PROFILE::ScopedCpuTimer cpuPost(
                    CPU_PROFILE::Pass::PostResolve);
                GFX::PIX::ScopedGpuEvent pixPost(
                    GFX::GetCurrentContext().cmdList,
                    GFX::PIX::kColorPost,
                    "PostSystem");
                GFX::GPU_PROFILE::ScopedGpuTimer gpuPost(
                    GFX::GetCurrentContext().cmdList,
                    GFX::GPU_PROFILE::Pass::PostResolve);
                if (!HIKARI::POST::PostSystem::PrepareGameUiComposition(
                        editorViewport,
                        gLogicalScreenWidth,
                        gLogicalScreenHeight)) {
                    HIKARI_LOG_ERROR("Game UI composition preparation failed.");
                    AbortActivePrimaryTemporalFrame();
                    return false;
                }
            }

            {
                CPU_PROFILE::ScopedCpuTimer cpuUi(
                    CPU_PROFILE::Pass::UiLayers);
                GFX::PIX::ScopedGpuEvent pixUi(
                    GFX::GetCurrentContext().cmdList,
                    GFX::PIX::kColorEditor,
                    "Game UI and Debug Layers");
                GFX::GPU_PROFILE::ScopedGpuTimer gpuUi(
                    GFX::GetCurrentContext().cmdList,
                    GFX::GPU_PROFILE::Pass::UiLayers);
                HIKARI::RENDERER::RenderLayerRange(
                    HIKARI::RENDERER::RenderLayer::UI,
                    HIKARI::RENDERER::RenderLayer::UI,
                    false);
                if (!HIKARI::GAME_UI::RenderGameUiFrame(
                        HIKARI::TIME::GetFrameContext().frameIndex,
                        gLogicalScreenWidth,
                        gLogicalScreenHeight)) {
                    HIKARI_LOG_ERROR(
                        "Game project UI effect rendering failed.");
                }
                HIKARI::RENDERER::RenderLayerRange(
                    HIKARI::RENDERER::RenderLayer::Debug,
                    HIKARI::RENDERER::RenderLayer::Debug,
                    true);
            }

            if (!HIKARI::POST::PostSystem::FinalizeGameUiComposition(
                    editorViewport)) {
                HIKARI_LOG_ERROR("Game UI composition finalization failed.");
                AbortActivePrimaryTemporalFrame();
                return false;
            }

            const HIKARI::POST::PresentationFrameResources& presentation =
                HIKARI::POST::PostSystem::GetPresentationFrameResources();
            RENDER3D::UPSCALING::SynchronizeStreamlineFrameGenerationPolicy(
                RENDER3D::GetRenderQualitySettings());
            if (presentation.HasFrameGenerationInputs()) {
                RENDER3D::TEMPORAL::TemporalInputs temporal{};
                if (HIKARI::POST::PostSystem::
                        TryGetPresentationTemporalInputs(temporal)) {
                    (void)RENDER3D::UPSCALING::
                        SubmitStreamlineFrameGenerationInputs(
                            temporal,
                            *presentation.hudlessColor,
                            *presentation.uiColorAndAlpha,
                            presentation.frameIndex,
                            IsGamePresentationActive());
                }
            }

            if (!HIKARI::POST::PostSystem::
                    FinishPresentationTemporalFrame(true)) {
                HIKARI_LOG_ERROR(
                    "Temporal presentation resource frame did not close.");
                return false;
            }
            gPresentationPrepared = true;
            return true;
        }

        bool EndFrame() {
            if (!gGpuFrameReady) {
                return false;
            }
            GFX::PIX::ScopedCpuEvent pixCpuFrame(GFX::PIX::kColorFrame, "Services.EndFrame");
            if (!PrepareFramePresentation()) {
                HIKARI::RENDERER::ClearSubmittedCommands();
            }
            if (RUNTIME::IEditorUiServices* editorUiServices =
                    RUNTIME::GetEditorUiServices()) {
                editorUiServices->RenderFrame(
                    GFX::GetCurrentContext(),
                    ShouldProduceEditorUiFrame());
            }

            gGraphicsCore.SetVSyncEnabled(RENDER3D::GetRenderQualitySettings().vSync);
            if (!gGraphicsCore.EndFrame()) {
                gGpuFrameReady = false;
                HIKARI_LOG_ERROR("D3D12 EndFrame failed; stopping GPU frame loop.");
                CPU_PROFILE::EndFrame();
                return false;
            }
            if (!RENDER3D::UPSCALING::
                    CaptureStreamlineFrameGenerationCompletionAfterPresent()) {
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
