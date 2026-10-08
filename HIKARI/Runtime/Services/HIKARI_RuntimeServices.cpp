#include "Core/HIKARI_StartupTimer.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Runtime/Presentation/HIKARI_GamePresentationService.h"
#include "Runtime/Services/Internal/HIKARI_RuntimeServiceState.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <objbase.h>
#include <sstream>
#include <utility>

#include "Audio/HIKARI_Audio.h"
#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Platform/Paths/HIKARI_UserDataPath.h"
#include "Render2D/HIKARI_Camera.h"
#include "Render2D/HIKARI_DxRenderer.h"
#include "Render2D/HIKARI_GameUiEffectRenderer.h"
#include "Render2D/HIKARI_DxTexture.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Lighting/HIKARI_VolumetricCloudStage.h"
#include "Render3D/Lighting/HIKARI_PanoramaSkyLayerStage.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Render3D/Material/HIKARI_DefaultPbrResources.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameGraphSession.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Surface/HIKARI_RenderFrameSurfaceRegistry.h"
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
auto& gActionTimeDilationService = INTERNAL::GetRuntimeServiceState().actionTimeDilationService;
auto& gGameProjectSaveStorage = INTERNAL::GetRuntimeServiceState().gameProjectSaveStorage;
auto& gGamePresentationController = INTERNAL::GetRuntimeServiceState().gamePresentationController;
auto& gComInitialized = INTERNAL::GetRuntimeServiceState().comInitialized;
auto& gRuntimeHostMode = INTERNAL::GetRuntimeServiceState().runtimeHostMode;
auto& gEnableImGui = INTERNAL::GetRuntimeServiceState().enableImGui;
auto& gEnableEditorUI = INTERNAL::GetRuntimeServiceState().enableEditorUI;
auto& gStreamlineEnabled = INTERNAL::GetRuntimeServiceState().streamlineEnabled;
auto& gRuntimeSessionEndRequested = INTERNAL::GetRuntimeServiceState().runtimeSessionEndRequested;
auto& gRuntimeStartupSceneGuid = INTERNAL::GetRuntimeServiceState().runtimeStartupSceneGuid;
auto& gRuntimeExportedSceneGuids = INTERNAL::GetRuntimeServiceState().runtimeExportedSceneGuids;
auto& gGpuFrameReady = INTERNAL::GetRuntimeServiceState().gpuFrameReady;
auto& gLogicalScreenWidth = INTERNAL::GetRuntimeServiceState().logicalScreenWidth;
auto& gLogicalScreenHeight = INTERNAL::GetRuntimeServiceState().logicalScreenHeight;
auto& gRuntimeWindowBaseTitle = INTERNAL::GetRuntimeServiceState().runtimeWindowBaseTitle;
auto& gPreviewTelemetrySeconds = INTERNAL::GetRuntimeServiceState().previewTelemetrySeconds;
auto& gPreviewTelemetryRenderFrames = INTERNAL::GetRuntimeServiceState().previewTelemetryRenderFrames;
auto& gPreviewTelemetryPresentedFrames = INTERNAL::GetRuntimeServiceState().previewTelemetryPresentedFrames;
} // namespace

RuntimeHostMode GetRuntimeHostMode() { return gRuntimeHostMode; }

INPUT::InputService& GetInputService() { return gInputService; }

const INPUT::InputSnapshot& GetInputSnapshot() { return gInputService.GetSnapshot(); }

ActionTimeDilationService& GetActionTimeDilationService() { return gActionTimeDilationService; }

GameProjectSaveStorageService& GetGameProjectSaveStorageService() { return gGameProjectSaveStorage; }

bool IsEditorHost() { return IsEditorHostMode(gRuntimeHostMode); }

bool IsStandaloneGameHost() { return IsStandaloneGameHostMode(gRuntimeHostMode); }

bool IsEditorUIEnabled() { return gEnableImGui && gEnableEditorUI; }

bool ShouldProduceEditorUiFrame() {
    return IsEditorHost() && IsEditorUIEnabled() && !IsInProcessGamePresentationActive();
}

int GetLogicalScreenWidth() { return gLogicalScreenWidth; }

int GetLogicalScreenHeight() { return gLogicalScreenHeight; }

void RequestRuntimeSessionEnd() { gRuntimeSessionEndRequested = true; }

bool IsRuntimeSessionEndRequested() { return gRuntimeSessionEndRequested; }

bool ConsumeRuntimeSessionEndRequest() {
    const bool requested = gRuntimeSessionEndRequested;
    gRuntimeSessionEndRequested = false;
    return requested;
}

const std::string& GetRuntimeStartupSceneGuid() { return gRuntimeStartupSceneGuid; }

bool IsRuntimeSceneGuidAllowed(const std::string& sceneGuid) {
    if (gRuntimeExportedSceneGuids.empty()) {
        return true;
    }
    return std::find(gRuntimeExportedSceneGuids.begin(), gRuntimeExportedSceneGuids.end(), sceneGuid) !=
           gRuntimeExportedSceneGuids.end();
}

void SetEditorUIEnabled(bool enabled) { gEnableEditorUI = IsEditorHost() && enabled; }

bool Initialize(const char* title, const RUNTIME::HostBootstrapConfig& cfg, std::string* error, bool startHidden) {
    if (error)
        error->clear();
    const auto fail = [error](std::string message) {
        HIKARI_LOG_ERROR(message);
        if (error)
            *error = std::move(message);
        return false;
    };
    gActionTimeDilationService.Reset();
    gRuntimeSessionEndRequested = false;
    INTERNAL::GetRuntimeServiceState().renderQualityRuntimeInitialized = false;
    INTERNAL::GetRuntimeServiceState().renderQualityTransitionCount = 0u;
    std::filesystem::path runtimeUserRoot{};
    const ProjectContext& launchProject = GetProjectContext();
    if (IsStandaloneGameHostMode(cfg.hostMode) && launchProject.IsOpen()) {
        runtimeUserRoot = PLATFORM::GetHikariGameUserRoot(launchProject.GetDescriptor().name);
    }
    PLATFORM::SetHikariRuntimeUserRoot(runtimeUserRoot);
    // 単体ゲームはユーザー領域、エディタは開いているプロジェクトの Library に記録する。
    const std::filesystem::path logRoot = !runtimeUserRoot.empty() ? runtimeUserRoot / "Logs"
                                          : launchProject.IsOpen() ? launchProject.GetLibraryRoot() / "Logs"
                                                                   : std::filesystem::path{};
    CORE::InitializeLogger(logRoot);
    CORE::StartupTimer servicesTimer("Services.initialize");
    HIKARI_LOG_INFO("HIKARI boot started.");
    {
        const RENDER3D::RenderQualitySettings& quality = RENDER3D::GetRenderQualitySettings();
        std::ostringstream oss;
        oss << "Render quality resolved"
            << " scene=" << RENDER3D::RenderResolutionPresetLabel(quality.sceneResolution)
            << " window=" << RENDER3D::RenderResolutionPresetLabel(quality.windowSize)
            << " aa=" << RENDER3D::RenderAntiAliasingModeLabel(quality.antiAliasingMode)
            << " dlss=" << RENDER3D::DlssQualityModeLabel(quality.dlssQualityMode)
            << " frameGeneration=" << RENDER3D::RenderFrameGenerationModeLabel(quality.frameGenerationMode)
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
            << " imgui=" << (gEnableImGui ? "true" : "false") << " editorUI=" << (gEnableEditorUI ? "true" : "false")
            << " startupSceneGuid=" << (gRuntimeStartupSceneGuid.empty() ? "<project>" : gRuntimeStartupSceneGuid)
            << " exportedSceneCount=" << gRuntimeExportedSceneGuids.size();
        HIKARI_LOG_INFO(oss.str());
    }

    HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    gComInitialized = SUCCEEDED(coHr);
    if (gComInitialized) {
        HIKARI_LOG_INFO("COM initialized.");
    } else {
        std::ostringstream oss;
        oss << "COM initialization failed. hr=0x" << std::hex << static_cast<unsigned long>(coHr);
        HIKARI_LOG_ERROR(oss.str());
    }

    const std::wstring wTitle = TEXT::PathFromUtf8(title != nullptr ? title : "").wstring();
    gRuntimeWindowBaseTitle = wTitle;
    gPreviewTelemetrySeconds = 0.0;
    gPreviewTelemetryRenderFrames = 0;
    gPreviewTelemetryPresentedFrames = 0;

    CORE::StartupTimer windowTimer("Window.initialize");
    HIKARI_LOG_INFO("Window initialization started.");
    if (!gWindow.Initialize(wTitle.c_str(), cfg.windowWidth, cfg.windowHeight, cfg.resizableWindow,
                            PLATFORM::WindowCloseBehavior::QuitApplication, true, startHidden)) {
        HIKARI_LOG_ERROR("Window initialization failed.");
        if (gStreamlineEnabled) {
            gStreamlineEnabled = !RENDER3D::UPSCALING::ShutdownStreamline();
        }
        return fail("Could not create the application window.");
    }
    GFX::PIX::Initialize(gWindow.GetHWND());
    {
        std::ostringstream oss;
        oss << "Window initialized. size=" << cfg.windowWidth << "x" << cfg.windowHeight
            << " resizable=" << (cfg.resizableWindow ? "true" : "false");
        HIKARI_LOG_INFO(oss.str());
    }
    windowTimer.Finish();
    CORE::StartupTimer audioTimer("Audio.initialize");
    if (AUDIO::Initialize(cfg.headlessRender ? AUDIO::BackendType::Null : AUDIO::BackendType::XAudio2)) {
        HIKARI_LOG_INFO("Audio initialized.");
    } else {
        HIKARI_LOG_WARN("Audio initialized with Null fallback.");
    }
    audioTimer.Finish();
    // 独立した音声デコードを GPU 初期化・起動演出の時間に重ねる。
    // Shipping はパッケージ済み RuntimePayload のみを先読みする。
    if (IsExportedGameHostMode(cfg.hostMode) && launchProject.IsOpen()) {
        AUDIO::PreloadDirectory(launchProject.Resolve("RuntimePayload"));
    }

    CORE::StartupTimer streamlineTimer("Streamline.early");
    gStreamlineEnabled = cfg.enableStreamline;
    // DLSS-G の読み込みは起動を 2.5〜3 秒延ばす。エディタはプロジェクトの画質設定で有効な時だけ読み込み（途中で有効に
    // した時は再起動で反映）、単体ゲームはプレイヤーが途中で切り替えられるよう常に読み込む。
    const bool enableFrameGenerationPlugins =
        IsStandaloneGameHostMode(cfg.hostMode) ||
        (IsEditorHostMode(cfg.hostMode) &&
         RENDER3D::GetRenderQualitySettings().frameGenerationMode == RENDER3D::RenderFrameGenerationMode::Dlss);
    if (gStreamlineEnabled) {
        gStreamlineEnabled = RENDER3D::UPSCALING::InitializeStreamlineEarly(
            enableFrameGenerationPlugins, CORE::GetLogFilePath().parent_path() / "Streamline");
    } else {
        HIKARI_LOG_INFO("[Streamline] bootstrap disabled for this host.");
    }
    streamlineTimer.Finish();

    CORE::StartupTimer graphicsTimer("D3D12.initialize");
    HIKARI_LOG_INFO("D3D12 core initialization started.");
    GFX::GraphicsBootstrapCallbacks graphicsCallbacks{};
    graphicsCallbacks.acceptDevice = [](ID3D12Device* device, std::string& message) {
        const auto caps = GFX::QueryShaderDeviceCapabilities(device);
        if (caps.shaderModel < 60) {
            message = "Shader Model 6.0 is required. Update the graphics driver or select a compatible GPU.";
            return false;
        }
        if (RENDER3D::GetRenderQualitySettings().geometryPipeline == RENDER3D::GeometryPipelineMode::MeshShader &&
            (caps.shaderModel < 65 || !(caps.featureMask & GFX::ShaderDeviceFeature::MeshShader))) {
            message =
                "This project's Mesh Shader renderer requires Shader Model 6.5 and Mesh Shader support. Update the driver or use a compatible GPU.";
            return false;
        }
        return true;
    };
    if (gStreamlineEnabled) {
        graphicsCallbacks.deviceCreated = [](ID3D12Device* device) {
            CORE::StartupTimer attachTimer("Streamline.attach_device");
            (void)RENDER3D::UPSCALING::AttachStreamlineDevice(device);
            if (IsEditorHost() && !RENDER3D::UPSCALING::SetStreamlineFrameGenerationFeatureLoaded(false)) {
                HIKARI_LOG_WARN("[Streamline] could not unload DLSS-G before creating the editor swap chain.");
            }
        };
        graphicsCallbacks.swapChainCreated = [](IDXGISwapChain4* swapChain) {
            RENDER3D::UPSCALING::InspectStreamlineSwapChain(swapChain);
        };
    }
    // 交换链必须按窗口 DPI 缩放后的实际客户端像素创建，
    // 否则 150% 缩放下窗口 2880x1620 但交换链只有 1920x1080，内容被截断。
    const int physicalClientWidth = (std::max)(gWindow.Width(), 16);
    const int physicalClientHeight = (std::max)(gWindow.Height(), 16);
    if (!gGraphicsCore.Initialize(gWindow.GetHWND(), physicalClientWidth, physicalClientHeight, cfg.enableDebugLayer,
                                  graphicsCallbacks)) {
        HIKARI_LOG_ERROR("D3D12 core initialization failed.");
        if (gStreamlineEnabled) {
            gStreamlineEnabled = !RENDER3D::UPSCALING::ShutdownStreamline();
        }
        return fail(gGraphicsCore.InitializationError().empty()
                        ? "Could not initialize Direct3D 12. See the log for the graphics driver error."
                        : gGraphicsCore.InitializationError());
    }
    GFX::FrameSubmissionCallbacks submissionCallbacks{};
    submissionCallbacks.presentStart = [headless = cfg.headlessRender] {
        // 最初の描画コマンドを送出してから本体ウィンドウを公開する。
        if (gWindow.IsStartupHidden() && !headless)
            gWindow.Show();
        if (gStreamlineEnabled)
            RENDER3D::UPSCALING::MarkStreamlineReflexPresentStart();
    };
    if (gStreamlineEnabled) {
        submissionCallbacks.renderSubmitStart = [] { RENDER3D::UPSCALING::MarkStreamlineReflexRenderSubmitStart(); };
        submissionCallbacks.renderSubmitEnd = [] { RENDER3D::UPSCALING::MarkStreamlineReflexRenderSubmitEnd(); };
        submissionCallbacks.presentEnd = [] { RENDER3D::UPSCALING::MarkStreamlineReflexPresentEnd(); };
    }
    gGraphicsCore.SetFrameSubmissionCallbacks(std::move(submissionCallbacks));
    graphicsTimer.Finish();
    HIKARI_LOG_INFO("D3D12 core initialized.");

    const int logicalScreenW = cfg.windowWidth;
    const int logicalScreenH = cfg.windowHeight;
    gLogicalScreenWidth = logicalScreenW;
    gLogicalScreenHeight = logicalScreenH;

    gWindow.SetResizeCallback([](int w, int h) { QueueWindowResize(w, h); });

    GFX::PublishCurrentContext(gGraphicsCore.BuildContext());
    const GFX::Context& graphicsContext = GFX::GetCurrentContext();
    if (gStreamlineEnabled) {
        RENDER3D::UPSCALING::UpdateStreamlineContext(graphicsContext);
    }

    CORE::StartupTimer textureTimer("Textures.initialize");
    if (!DXTEX::DxTextureManager::Init(graphicsContext)) {
        return fail("Could not initialize texture resources. Check available graphics memory and the driver.");
    }
    textureTimer.Finish();
    HIKARI_LOG_INFO("TextureManager initialized.");
    RENDER3D::UpdateRenderResourceDescriptorPoolContext(graphicsContext);
    RENDER3D::UpdateClusterGeometryResourceContext(graphicsContext);
    CORE::StartupTimer pbrTimer("PbrDefaults.initialize");
    HIKARI::DefaultPbrResources::Initialize();
    pbrTimer.Finish();
    HIKARI_LOG_INFO("Default PBR resources initialized.");
    CORE::StartupTimer rendererTimer("Renderer2D.initialize");
    if (!DX::DxRenderer::Init(graphicsContext)) {
        return fail(
            "Could not initialize the 2D renderer. Check the shader package, graphics capabilities and available memory.");
    }
    rendererTimer.Finish();
    HIKARI_LOG_INFO("DxRenderer initialized.");
    CORE::StartupTimer postTimer("Post.initialize");
    POST::PostSystem::Initialize(graphicsContext);
    postTimer.Finish();
    HIKARI_LOG_INFO("PostSystem initialized.");
    const ProjectContext& projectContext = GetProjectContext();
    const bool inputInitialized = projectContext.IsOpen() ? gInputService.Initialize(projectContext.GetRoot())
                                                          : gInputService.InitializeTransient();
    if (!inputInitialized) {
        return fail("Input service initialization failed.");
    }
    if (projectContext.IsOpen()) {
        std::string saveStorageMessage{};
        if (!gGameProjectSaveStorage.Configure(projectContext.GetRoot(), {}, &saveStorageMessage)) {
            HIKARI_LOG_ERROR("GameProject save storage initialization failed: " + saveStorageMessage);
            return fail("Could not initialize save storage: " + saveStorageMessage);
        }
    } else {
        gGameProjectSaveStorage.Reset();
    }
    gInputService.Contexts().SetActive("Editor", IsEditorHost());
    gInputService.Contexts().SetActive("Gameplay", !IsEditorHost());
    gInputService.SetHostWindow(gWindow.GetHWND());
    gInputService.ClearMouseCaptureRegion();
    gInputService.SetMouseCaptureMode(IsStandaloneGameHost() ? INPUT::MouseCaptureMode::Relative
                                                             : INPUT::MouseCaptureMode::Free);

    HIKARI::CAMERA::SetScreenSize(cfg.windowWidth, cfg.windowHeight);
    HIKARI::CAMERA::SetScreenCenter({0.0f, 0.0f});
    HIKARI::CAMERA::EnableDebugControl(cfg.enableDebugCamera);
    HIKARI_LOG_INFO("Camera initialized.");

    if (gEnableImGui) {
        RUNTIME::IEditorUiServices* editorUiServices = RUNTIME::GetEditorUiServices();
        if (!IsEditorHost() || editorUiServices == nullptr || !editorUiServices->Initialize(gWindow, graphicsContext)) {
            HIKARI_LOG_WARN("Editor UI services are unavailable; editor UI disabled.");
            gEnableImGui = false;
            gEnableEditorUI = false;
        }
    } else {
        gEnableImGui = false;
        gEnableEditorUI = false;
    }
    HIKARI_LOG_INFO("HIKARI boot completed.");
    return true;
}

void FinalizeAll() {
    HIKARI_LOG_INFO("HIKARI shutdown started.");
    gActionTimeDilationService.Reset();
    gGameProjectSaveStorage.Reset();
    gRuntimeSessionEndRequested = false;
    gInputService.Shutdown();
    bool presentationTeardownSafe = true;
    if (IsInProcessGamePresentationActive() && !EndInProcessGamePresentation()) {
        HIKARI_LOG_ERROR("In-process presentation could not be fully stopped during shutdown.");
        presentationTeardownSafe = false;
        const bool presentationQueueIdle = !gGpuFrameReady && gGraphicsCore.WaitForIdle();
        const bool frameGenerationStopped =
            presentationQueueIdle && RENDER3D::UPSCALING::DeactivateStreamlineFrameGeneration(true);
        if (frameGenerationStopped) {
            presentationTeardownSafe = gGamePresentationController.IsGameSurfaceReleased() ||
                                       gGamePresentationController.ReleaseGameSurface(gGraphicsCore);
        }
    }
    const bool rendererGpuIdle = gGraphicsCore.WaitForIdle();
    const bool gpuOwnerTeardownEligible = presentationTeardownSafe && (rendererGpuIdle || gGraphicsCore.IsDeviceLost());
    if (RUNTIME::IEditorUiServices* editorUiServices = RUNTIME::GetEditorUiServices();
        editorUiServices != nullptr && gpuOwnerTeardownEligible) {
        editorUiServices->Shutdown(gWindow);
    }
    if (!rendererGpuIdle) {
        HIKARI_LOG_ERROR("GPU idle wait failed before renderer resource shutdown.");
    }
    const RENDER3D::TEMPORAL::TemporalResourceReleaseMode temporalReleaseMode =
        rendererGpuIdle
            ? RENDER3D::TEMPORAL::TemporalResourceReleaseMode::ImmediateAfterGpuIdle
            : (gGraphicsCore.IsDeviceLost() ? RENDER3D::TEMPORAL::TemporalResourceReleaseMode::ImmediateAfterDeviceLost
                                            : RENDER3D::TEMPORAL::TemporalResourceReleaseMode::DeferredUntilFence);
    const bool streamlineShutdownSafe =
        gpuOwnerTeardownEligible && (!gStreamlineEnabled || RENDER3D::UPSCALING::ShutdownStreamline());
    GFX::GraphicsShutdownResult graphicsShutdownResult = GFX::GraphicsShutdownResult::UnsafeWaitFailure;
    if (streamlineShutdownSafe) {
        if (gStreamlineEnabled) {
            HIKARI_LOG_INFO("Streamline shutdown.");
        }
        MODELRENDERER::Reset();
        HIKARI_LOG_INFO("Model renderer and scene captures finalized.");
        MESHRENDERER::ShutdownParticleGpuDrivenPass();
        HIKARI_LOG_INFO("Particle GPU-driven pass finalized.");
        MESHRENDERER::ShutdownWorldSpritePass();
        HIKARI_LOG_INFO("World Sprite pass finalized.");
        MESHRENDERER::ShutdownWorldTextPass();
        HIKARI_LOG_INFO("World Text pass finalized.");
        MESHRENDERER::ShutdownOceanSpectrumRuntime();
        HIKARI_LOG_INFO("Ocean spectrum runtime finalized.");
        HIKARI::POST::PostSystem::Shutdown();
        HIKARI_LOG_INFO("PostSystem shutdown.");
        if (!RENDER3D::PIPELINE::ShutdownRenderFrameGraphSessions()) {
            HIKARI_LOG_ERROR("Render frame graph session shutdown rejected "
                             "the current thread or retained an active session.");
        } else {
            HIKARI_LOG_INFO("Render frame graph sessions shutdown.");
        }
        if (!RENDER3D::SURFACE::ShutdownRenderFrameSurfaceRegistry()) {
            HIKARI_LOG_ERROR("Render frame surface registry shutdown rejected "
                             "the current thread.");
        } else {
            HIKARI_LOG_INFO("Render frame surface registry shutdown.");
        }
        RENDER3D::PIPELINE::ShutdownRenderFramePipelineResources();
        ASSETS::SHADER::ResetBuiltinShaderArtifactResolver();
        HIKARI_LOG_INFO("Render frame pipeline resources shutdown.");
        GAME_UI::ShutdownGameUiRenderer();
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
        RENDER3D::TEMPORAL::ShutdownTemporalResourceSystem(temporalReleaseMode);
        RENDER3D::TEMPORAL::ShutdownTemporalFrameStateSystem();
        RENDER3D::CLOUDS::ShutdownVolumetricCloudStage();
        RENDER3D::PANORAMA_SKY::ShutdownPanoramaSkyLayerStage();
        RENDER3D::VOLUMETRIC::ShutdownVolumetricLightingStage();
        SKYRENDERER::ShutdownAtmosphereRuntime();
        RENDER3D::WATER::ShutdownWaterVisibleDepth();
        RENDER3D::ShutdownClusterGeometryResourceSystem();
        DXTEX::DxTextureManager::Finalize();
        HIKARI_LOG_INFO("TextureManager finalized.");
        gGamePresentationController.Shutdown();
        graphicsShutdownResult = gGraphicsCore.Shutdown();
        if (graphicsShutdownResult != GFX::GraphicsShutdownResult::UnsafeWaitFailure) {
            // Dx12Core drained deferred descriptor callbacks. The
            // allocator remains alive until every callback returns.
            RENDER3D::ShutdownRenderResourceDescriptorPool();
        } else {
            HIKARI_LOG_ERROR("Descriptor pool retained because GPU shutdown "
                             "safety could not be established.");
        }
    } else if (!gpuOwnerTeardownEligible) {
        HIKARI_LOG_ERROR("GPU idle could not be proven and the device is not "
                         "reported lost. All GPU owners are retained for process "
                         "teardown.");
    } else {
        HIKARI_LOG_ERROR("Streamline input completion was not proven. GPU "
                         "resources are retained for process teardown.");
    }
    AUDIO::Shutdown();
    HIKARI_LOG_INFO("Audio shutdown.");
    if (streamlineShutdownSafe) {
        gStreamlineEnabled = false;
    }
    GFX::ClearCurrentContext();
    if (!streamlineShutdownSafe || graphicsShutdownResult == GFX::GraphicsShutdownResult::UnsafeWaitFailure) {
        HIKARI_LOG_ERROR("D3D12 core retained for unsafe process teardown.");
    } else {
        HIKARI_LOG_INFO("D3D12 core shutdown.");
    }
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
    INTERNAL::GetRuntimeServiceState().renderQualityRuntimeInitialized = false;
    INTERNAL::GetRuntimeServiceState().renderQualityTransitionCount = 0u;
    HIKARI_LOG_INFO("HIKARI shutdown completed.");
    CORE::ShutdownLogger();
}

bool PumpMessages() { return gWindow.PumpMessages(); }
} // namespace SERVICES
} // namespace HIKARI
