#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Runtime/Presentation/HIKARI_GamePresentationService.h"
#include "Runtime/Services/Internal/HIKARI_RuntimeServiceState.h"

#include <algorithm>
#include <iomanip>
#include <objbase.h>
#include <sstream>
#include <utility>

#include "Audio/HIKARI_Audio.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Render2D/HIKARI_Camera.h"
#include "Render2D/HIKARI_DxRenderer.h"
#include "Render2D/HIKARI_DxTexture.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Lighting/HIKARI_StylizedCloudStage.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Material/HIKARI_DefaultPbrResources.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Temporal/HIKARI_TaaResolvePass.h"
#include "Render3D/Temporal/HIKARI_TemporalGeometryPass.h"
#include "Render3D/Temporal/HIKARI_TemporalMaskPass.h"
#include "Render3D/Temporal/HIKARI_TemporalMotionVectorPass.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"
#include "Render3D/Upscaling/HIKARI_StreamlineReflex.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Render3D/Water/HIKARI_WaterVisibleDepth.h"
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

        ActionTimeDilationService& GetActionTimeDilationService() {
            return gActionTimeDilationService;
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
            gActionTimeDilationService.Reset();
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
            // 交换链必须按窗口 DPI 缩放后的实际客户端像素创建，
            // 否则 150% 缩放下窗口 2880x1620 但交换链只有 1920x1080，内容被截断。
            const int physicalClientWidth = (std::max)(gWindow.Width(), 16);
            const int physicalClientHeight = (std::max)(gWindow.Height(), 16);
            if (!gGraphicsCore.Initialize(
                    gWindow.GetHWND(),
                    physicalClientWidth,
                    physicalClientHeight,
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
            gGraphicsCore.SetFrameSubmissionCallbacks(std::move(submissionCallbacks));
            HIKARI_LOG_INFO("D3D12 core initialized.");

            const int logicalScreenW = cfg.windowWidth;
            const int logicalScreenH = cfg.windowHeight;
            gLogicalScreenWidth = logicalScreenW;
            gLogicalScreenHeight = logicalScreenH;

            gWindow.SetResizeCallback([](int w, int h) {
                QueueWindowResize(w, h);
            });

            GFX::PublishCurrentContext(gGraphicsCore.BuildContext());
            const GFX::Context& graphicsContext = GFX::GetCurrentContext();
            RENDER3D::UPSCALING::UpdateStreamlineContext(graphicsContext);

            DXTEX::DxTextureManager::Init(graphicsContext);
            HIKARI_LOG_INFO("TextureManager initialized.");
            RENDER3D::UpdateRenderResourceDescriptorPoolContext(graphicsContext);
            RENDER3D::UpdateClusterGeometryResourceContext(graphicsContext);
            HIKARI::DefaultPbrResources::Initialize();
            HIKARI_LOG_INFO("Default PBR resources initialized.");
            DX::DxRenderer::Init(graphicsContext);
            HIKARI_LOG_INFO("DxRenderer initialized.");
            POST::PostSystem::Initialize(graphicsContext);
            HIKARI_LOG_INFO("PostSystem initialized.");
            if (AUDIO::Initialize(AUDIO::BackendType::XAudio2)) {
                HIKARI_LOG_INFO("Audio initialized.");
            }
            else {
                HIKARI_LOG_WARN("Audio initialized with Null fallback.");
            }
            const ProjectContext& projectContext =
                GetProjectContext();
            const bool inputInitialized = projectContext.IsOpen()
                ? gInputService.Initialize(projectContext.GetRoot())
                : gInputService.InitializeTransient();
            if (!inputInitialized) {
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
                    !editorUiServices->Initialize(gWindow, graphicsContext)) {
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
            gActionTimeDilationService.Reset();
            gInputService.Shutdown();
            if (IsInProcessGamePresentationActive() &&
                !EndInProcessGamePresentation()) {
                HIKARI_LOG_ERROR(
                    "In-process presentation could not be fully stopped during shutdown.");
                (void)RENDER3D::UPSCALING::
                    DeactivateStreamlineFrameGeneration(true);
                if (!gGamePresentationController.IsGameSurfaceReleased() &&
                    gGraphicsCore.WaitForIdle()) {
                    (void)gGamePresentationController.ReleaseGameSurface(gGraphicsCore);
                }
            }
            if (RUNTIME::IEditorUiServices* editorUiServices =
                    RUNTIME::GetEditorUiServices()) {
                if (!gGraphicsCore.WaitForIdle()) {
                    HIKARI_LOG_ERROR(
                        "GPU idle wait failed before editor UI shutdown.");
                }
                editorUiServices->Shutdown(gWindow);
            }
            if (!gGraphicsCore.WaitForIdle()) {
                HIKARI_LOG_ERROR(
                    "GPU idle wait failed before renderer resource shutdown.");
            }
            MESHRENDERER::ShutdownParticleGpuDrivenPass();
            HIKARI_LOG_INFO("Particle GPU-driven pass finalized.");
            RENDER3D::PIPELINE::ShutdownCombatPresentationRenderers();
            HIKARI_LOG_INFO("Combat presentation renderers finalized.");
            MESHRENDERER::ShutdownOceanSpectrumRuntime();
            HIKARI_LOG_INFO("Ocean spectrum runtime finalized.");
            HIKARI::POST::PostSystem::Shutdown();
            HIKARI_LOG_INFO("PostSystem shutdown.");
            DX::DxRenderer::Finalize();
            HIKARI_LOG_INFO("DxRenderer finalized.");
            RENDER3D::MATERIAL::ResetRuntimeMaterialProgramRegistry();
            HIKARI_LOG_INFO("Runtime material programs finalized.");
            HIKARI::DefaultPbrResources::Shutdown();
            HIKARI_LOG_INFO("Default PBR resources finalized.");
            RENDER3D::TEMPORAL::ShutdownTaaResolvePass();
            RENDER3D::TEMPORAL::ShutdownTemporalMaskPass();
            RENDER3D::TEMPORAL::ShutdownTemporalGeometryPass();
            RENDER3D::TEMPORAL::ShutdownMotionVectorPass();
            RENDER3D::TEMPORAL::ShutdownTemporalResourceSystem();
            RENDER3D::CLOUDS::ShutdownStylizedCloudStage();
            RENDER3D::VOLUMETRIC::ShutdownVolumetricLightingStage();
            SKYRENDERER::ShutdownAtmosphereRuntime();
            RENDER3D::WATER::ShutdownWaterVisibleDepth();
            RENDER3D::ShutdownClusterGeometryResourceSystem();
            RENDER3D::ShutdownRenderResourceDescriptorPool();
            DXTEX::DxTextureManager::Finalize();
            HIKARI_LOG_INFO("TextureManager finalized.");
            AUDIO::Shutdown();
            HIKARI_LOG_INFO("Audio shutdown.");
            RENDER3D::UPSCALING::ShutdownStreamline();
            HIKARI_LOG_INFO("Streamline shutdown.");
            gGamePresentationController.Shutdown();
            gGraphicsCore.Shutdown();
            GFX::ClearCurrentContext();
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
