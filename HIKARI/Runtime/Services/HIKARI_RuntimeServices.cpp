#include "HIKARI_Services.h"
#include "Runtime/Services/Internal/HIKARI_RuntimeServiceState.h"

#include <objbase.h>

namespace HIKARI {
    namespace SERVICES {
        namespace {
            auto& gWindow = INTERNAL::GetRuntimeServiceState().window;
            auto& gInputService = INTERNAL::GetRuntimeServiceState().inputService;
            auto& gGamePresentationController =
                INTERNAL::GetRuntimeServiceState().gamePresentationController;
            auto& gComInitialized =
                INTERNAL::GetRuntimeServiceState().comInitialized;
            auto& gRuntimeHostMode =
                INTERNAL::GetRuntimeServiceState().runtimeHostMode;
            auto& gEnableImGui = INTERNAL::GetRuntimeServiceState().enableImGui;
            auto& gEnableEditorUI =
                INTERNAL::GetRuntimeServiceState().enableEditorUI;
            auto& gRuntimeStartupSceneGuid =
                INTERNAL::GetRuntimeServiceState().runtimeStartupSceneGuid;
            auto& gRuntimeExportedSceneGuids =
                INTERNAL::GetRuntimeServiceState().runtimeExportedSceneGuids;
            auto& gGpuFrameReady =
                INTERNAL::GetRuntimeServiceState().gpuFrameReady;
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

        RuntimeHostMode GetRuntimeHostMode() { return gRuntimeHostMode; }

        INPUT::InputService& GetInputService() { return gInputService; }

        const INPUT::InputSnapshot& GetInputSnapshot() {
            return gInputService.GetSnapshot();
        }

        bool IsEditorHost() { return IsEditorHostMode(gRuntimeHostMode); }

        bool IsStandaloneGameHost() { return IsStandaloneGameHostMode(gRuntimeHostMode); }

        bool IsImGuiEnabled() { return gEnableImGui; }

        bool IsEditorUIEnabled() { return gEnableImGui && gEnableEditorUI; }

        bool ShouldProduceEditorUiFrame() {
            return IsEditorHost() &&
                IsEditorUIEnabled() &&
                !IsInProcessGamePresentationActive();
        }

        const std::string& GetRuntimeStartupSceneGuid() { return gRuntimeStartupSceneGuid; }

        const std::vector<std::string>& GetRuntimeExportedSceneGuids() { return gRuntimeExportedSceneGuids; }

        bool IsRuntimeSceneGuidAllowed(const std::string& sceneGuid) {
            if (gRuntimeExportedSceneGuids.empty()) {
                return true;
            }
            return std::find(
                gRuntimeExportedSceneGuids.begin(),
                gRuntimeExportedSceneGuids.end(),
                sceneGuid) != gRuntimeExportedSceneGuids.end();
        }

        void SetEditorUIEnabled(bool enabled) { gEnableEditorUI = IsEditorHost() && enabled; }

        bool Initialize(
            const char* title,
            const RUNTIME::HostBootstrapConfig& cfg) {
            const bool enableFrameGenerationPlugins =
                IsEditorHostMode(cfg.hostMode) ||
                (IsStandaloneGameHostMode(cfg.hostMode) &&
                    RENDER3D::GetRenderQualitySettings().frameGenerationMode ==
                        RENDER3D::RenderFrameGenerationMode::Dlss);
            (void)RENDER3D::UPSCALING::InitializeStreamlineEarly(
                enableFrameGenerationPlugins);
            CORE::InitializeLogger();
            HIKARI_LOG_INFO("HIKARI boot started.");
            {
                const RENDER3D::RenderQualitySettings& quality =
                    RENDER3D::GetRenderQualitySettings();
                std::ostringstream oss;
                oss << "Render quality resolved"
                    << " scene=" << RENDER3D::RenderResolutionPresetLabel(quality.sceneResolution)
                    << " window=" << RENDER3D::RenderResolutionPresetLabel(quality.windowSize)
                    << " aa=" << RENDER3D::RenderAntiAliasingModeLabel(quality.antiAliasingMode)
                    << " dlss=" << RENDER3D::DlssQualityModeLabel(quality.dlssQualityMode)
                    << " frameGeneration="
                    << RENDER3D::RenderFrameGenerationModeLabel(quality.frameGenerationMode)
                    << " multiplier=" << static_cast<unsigned>(quality.frameGenerationMultiplier);
                HIKARI_LOG_INFO(oss.str());
            }

            gRuntimeHostMode = cfg.hostMode;
            gEnableImGui = cfg.enableImGui;
            gEnableEditorUI = IsEditorHostMode(gRuntimeHostMode) && cfg.enableEditorUI;
            gRuntimeStartupSceneGuid = cfg.startupSceneGuid;
            gRuntimeExportedSceneGuids = cfg.exportedSceneGuids;
            {
                std::ostringstream oss;
                oss << "Runtime host mode=" << RuntimeHostModeName(gRuntimeHostMode)
                    << " imgui=" << (gEnableImGui ? "true" : "false")
                    << " editorUI=" << (gEnableEditorUI ? "true" : "false")
                    << " startupSceneGuid=" << (gRuntimeStartupSceneGuid.empty() ? "<project>" : gRuntimeStartupSceneGuid)
                    << " exportedSceneCount=" << gRuntimeExportedSceneGuids.size();
                HIKARI_LOG_INFO(oss.str());
            }

            HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            gComInitialized = SUCCEEDED(coHr);
            if (gComInitialized) {
                HIKARI_LOG_INFO("COM initialized.");
            }
            else {
                std::ostringstream oss;
                oss << "COM initialization failed. hr=0x" << std::hex << static_cast<unsigned long>(coHr);
                HIKARI_LOG_ERROR(oss.str());
            }

            wchar_t wTitle[256]{};
            mbstowcs_s(nullptr, wTitle, title, _TRUNCATE);
            gRuntimeWindowBaseTitle = wTitle;
            gPreviewTelemetrySeconds = 0.0;
            gPreviewTelemetryRenderFrames = 0;
            gPreviewTelemetryPresentedFrames = 0;

            HIKARI_LOG_INFO("Window initialization started.");
            if (!gWindow.Initialize(wTitle, cfg.windowWidth, cfg.windowHeight, cfg.resizableWindow)) {
                HIKARI_LOG_ERROR("Window initialization failed.");
                RENDER3D::UPSCALING::ShutdownStreamline();
                return false;
            }
            GFX::PIX::Initialize(gWindow.GetHWND());
            {
                std::ostringstream oss;
                oss << "Window initialized. size=" << cfg.windowWidth << "x" << cfg.windowHeight
                    << " resizable=" << (cfg.resizableWindow ? "true" : "false");
                HIKARI_LOG_INFO(oss.str());
            }
            HIKARI_LOG_INFO("D3D12 core initialization started.");
            GFX::GraphicsBootstrapCallbacks graphicsCallbacks{};
            graphicsCallbacks.deviceCreated = [](ID3D12Device* device) {
                (void)RENDER3D::UPSCALING::AttachStreamlineDevice(device);
                if (IsEditorHost() &&
                    !RENDER3D::UPSCALING::SetStreamlineFrameGenerationFeatureLoaded(false)) {
                    HIKARI_LOG_WARN(
                        "[Streamline] could not unload DLSS-G before creating the editor swap chain.");
                }
            };
            graphicsCallbacks.swapChainCreated = [](IDXGISwapChain4* swapChain) {
                RENDER3D::UPSCALING::InspectStreamlineSwapChain(swapChain);
            };
            if (!gCore.Initialize(
                    gWindow.GetHWND(),
                    cfg.windowWidth,
                    cfg.windowHeight,
                    cfg.enableDebugLayer,
                    graphicsCallbacks)) {
                HIKARI_LOG_ERROR("D3D12 core initialization failed.");
                RENDER3D::UPSCALING::ShutdownStreamline();
                return false;
            }
            GFX::FrameSubmissionCallbacks submissionCallbacks{};
            submissionCallbacks.renderSubmitStart = [] {
                RENDER3D::UPSCALING::MarkStreamlineReflexRenderSubmitStart();
            };
            submissionCallbacks.renderSubmitEnd = [] {
                RENDER3D::UPSCALING::MarkStreamlineReflexRenderSubmitEnd();
            };
            submissionCallbacks.presentStart = [] {
                RENDER3D::UPSCALING::MarkStreamlineReflexPresentStart();
            };
            submissionCallbacks.presentEnd = [] {
                RENDER3D::UPSCALING::MarkStreamlineReflexPresentEnd();
            };
            gCore.SetFrameSubmissionCallbacks(std::move(submissionCallbacks));
            HIKARI_LOG_INFO("D3D12 core initialized.");

            const int logicalScreenW = cfg.windowWidth;
            const int logicalScreenH = cfg.windowHeight;
            gLogicalScreenWidth = logicalScreenW;
            gLogicalScreenHeight = logicalScreenH;

            gWindow.SetResizeCallback([](int w, int h) {
                QueueWindowResize(w, h);
            });

            gCtx = gCore.BuildContext();
            RENDER3D::UPSCALING::UpdateStreamlineContext(gCtx);

            DXTEX::DxTextureManager::Init(gCtx);
            HIKARI_LOG_INFO("TextureManager initialized.");
            RENDER3D::UpdateRenderResourceDescriptorPoolContext(gCtx);
            RENDER3D::UpdateClusterGeometryResourceContext(gCtx);
            HIKARI::DefaultPbrResources::Initialize();
            HIKARI_LOG_INFO("Default PBR resources initialized.");
            DX::DxRenderer::Init(gCtx);
            HIKARI_LOG_INFO("DxRenderer initialized.");
            POST::PostSystem::Initialize(gCtx);
            HIKARI_LOG_INFO("PostSystem initialized.");
            if (AUDIO::Initialize(AUDIO::BackendType::XAudio2)) {
                HIKARI_LOG_INFO("Audio initialized.");
            }
            else {
                HIKARI_LOG_WARN("Audio initialized with Null fallback.");
            }
            HIKARI::VFX::Initialize(gCtx);
            HIKARI_LOG_INFO("VFX initialized.");

            if (!gInputService.Initialize(std::filesystem::current_path())) {
                HIKARI_LOG_ERROR("Input service initialization failed.");
                return false;
            }
            gInputService.Contexts().SetActive(
                "Editor", IsEditorHost());
            gInputService.Contexts().SetActive(
                "Gameplay", !IsEditorHost());
            gInputService.SetHostWindow(gWindow.GetHWND());
            gInputService.ClearMouseCaptureRegion();
            gInputService.SetMouseCaptureMode(
                IsStandaloneGameHost()
                    ? INPUT::MouseCaptureMode::Relative
                    : INPUT::MouseCaptureMode::Free);

            HIKARI::CAMERA::SetScreenSize(cfg.windowWidth, cfg.windowHeight);
            HIKARI::CAMERA::SetScreenCenter({ 0.0f,0.0f });
            HIKARI::CAMERA::EnableDebugControl(cfg.enableDebugCamera);
            HIKARI_LOG_INFO("Camera initialized.");

            if (gEnableImGui) {
                RUNTIME::IEditorUiServices* editorUiServices =
                    RUNTIME::GetEditorUiServices();
                if (!IsEditorHost() ||
                    editorUiServices == nullptr ||
                    !editorUiServices->Initialize(gWindow, gCtx)) {
                    HIKARI_LOG_WARN(
                        "Editor UI services are unavailable; editor UI disabled.");
                    gEnableImGui = false;
                    gEnableEditorUI = false;
                }
            }
            else {
                gEnableImGui = false;
                gEnableEditorUI = false;
            }
            HIKARI_LOG_INFO("HIKARI boot completed.");
            return true;
        }

        void FinalizeAll() {
            HIKARI_LOG_INFO("HIKARI shutdown started.");
            gInputService.Shutdown();
            if (IsInProcessGamePresentationActive() &&
                !EndInProcessGamePresentation()) {
                HIKARI_LOG_ERROR(
                    "In-process presentation could not be fully stopped during shutdown.");
                (void)RENDER3D::UPSCALING::
                    DeactivateStreamlineFrameGeneration(true);
                if (!gGamePresentationController.IsGameSurfaceReleased() &&
                    gCore.WaitForIdle()) {
                    (void)gGamePresentationController.ReleaseGameSurface(gCore);
                }
            }
            if (RUNTIME::IEditorUiServices* editorUiServices =
                    RUNTIME::GetEditorUiServices()) {
                if (!gCore.WaitForIdle()) {
                    HIKARI_LOG_ERROR(
                        "GPU idle wait failed before editor UI shutdown.");
                }
                editorUiServices->Shutdown(gWindow);
            }
            HIKARI::POST::PostSystem::Shutdown();
            HIKARI_LOG_INFO("PostSystem shutdown.");
            DX::DxRenderer::Finalize();
            HIKARI_LOG_INFO("DxRenderer finalized.");
            HIKARI::DefaultPbrResources::Shutdown();
            HIKARI_LOG_INFO("Default PBR resources finalized.");
            RENDER3D::TEMPORAL::ShutdownTaaResolvePass();
            RENDER3D::TEMPORAL::ShutdownTemporalMaskPass();
            RENDER3D::TEMPORAL::ShutdownTemporalGeometryPass();
            RENDER3D::TEMPORAL::ShutdownMotionVectorPass();
            RENDER3D::TEMPORAL::ShutdownTemporalResourceSystem();
            RENDER3D::VOLUMETRIC::ShutdownVolumetricLightingStage();
            RENDER3D::ShutdownClusterGeometryResourceSystem();
            RENDER3D::ShutdownRenderResourceDescriptorPool();
            DXTEX::DxTextureManager::Finalize();
            HIKARI_LOG_INFO("TextureManager finalized.");
            HIKARI::VFX::Shutdown();
            HIKARI_LOG_INFO("VFX shutdown.");
            AUDIO::Shutdown();
            HIKARI_LOG_INFO("Audio shutdown.");
            RENDER3D::UPSCALING::ShutdownStreamline();
            HIKARI_LOG_INFO("Streamline shutdown.");
            gGamePresentationController.Shutdown();
            gCore.Shutdown();
            HIKARI_LOG_INFO("D3D12 core shutdown.");
            GFX::PIX::Shutdown();
            gWindow.Shutdown();
            HIKARI_LOG_INFO("Window shutdown.");
            if (gComInitialized) {
                CoUninitialize();
                gComInitialized = false;
                HIKARI_LOG_INFO("COM uninitialized.");
            }
            gRuntimeStartupSceneGuid.clear();
            gRuntimeExportedSceneGuids.clear();
            gRuntimeWindowBaseTitle.clear();
            gPreviewTelemetrySeconds = 0.0;
            gPreviewTelemetryRenderFrames = 0;
            gPreviewTelemetryPresentedFrames = 0;
            HIKARI_LOG_INFO("HIKARI shutdown completed.");
            CORE::ShutdownLogger();
        }

        bool PumpMessages() {
            return gWindow.PumpMessages();
        }
    } // namespace SERVICES
} // namespace HIKARI
