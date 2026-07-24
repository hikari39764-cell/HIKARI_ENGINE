#pragma once

#include "HIKARI_Core.h"
#include "HIKARI_2D.h"
#include "HIKARI_3D.h"

#include "HIKARI_Bgm.h"
#include "HIKARI_SE.h"
#include "HIKARI_Anim.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Runtime/HIKARI_RuntimeHost.h"
#include "Runtime/HIKARI_GamePresentationController.h"
#include "Vfx/Runtime/HIKARI_VfxSystem.h"

#include "Platform/HIKARI_Win32Window.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_Dx12Core.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Material/HIKARI_DefaultPbrResources.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#endif
#include "Render3D/Temporal/HIKARI_TaaResolvePass.h"
#include "Render3D/Temporal/HIKARI_TemporalMotionVectorPass.h"
#include "Render3D/Temporal/HIKARI_TemporalGeometryPass.h"
#include "Render3D/Temporal/HIKARI_TemporalMaskPass.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"
#include "Render3D/Lighting/HIKARI_VolumetricLightingStage.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"
#include "Render3D/Upscaling/HIKARI_StreamlineFrameGenerationPolicy.h"
#include "Render3D/Upscaling/HIKARI_StreamlineReflex.h"
#include "Audio/HIKARI_Audio.h"
#if defined(HIKARI_WITH_EDITOR)
#include "Editor/Style/HIKARI_EditorFontManager.h"
#include "Editor/HIKARI_EditorStyle.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#endif
#if defined(HIKARI_ENABLE_IMGUI)
#include "imgui.h"
#include "../ThirdParty/imgui/imgui_impl_dx12.h"
#include "../ThirdParty/imgui/imgui_impl_win32.h"
#endif
#include <objbase.h>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace HIKARI {
    namespace SERVICES {

        struct BootstrapConfig {
            RuntimeHostMode hostMode = DefaultRuntimeHostMode();
            bool enableDebugCamera = false;
            bool enableDebugLayer = true;
            bool resizableWindow = true;
            bool enableImGui =
#if defined(HIKARI_ENABLE_IMGUI)
                true;
#else
                false;
#endif
            bool enableEditorUI =
#if defined(HIKARI_WITH_EDITOR)
                true;
#else
                false;
#endif
            bool enablePortableObjectTools = false;
            int windowWidth = PRESENTATION::kDefaultLogicalWidth;
            int windowHeight = PRESENTATION::kDefaultLogicalHeight;
            std::string startupSceneGuid{};
            std::vector<std::string> exportedSceneGuids{};
        };

        inline PLATFORM::Win32Window gWindow{};
        inline INPUT::InputService gInputService{};
        inline RUNTIME::GamePresentationController gGamePresentationController{};
        inline GFX::Dx12Core gCore{};
        inline GFX::Context gCtx{};
        inline bool gComInitialized = false;
        inline bool gImGuiInitialized = false;
        inline bool gImGuiBackendInitialized = false;
        inline bool gImGuiFrameBegun = false;
        inline RuntimeHostMode gRuntimeHostMode = DefaultRuntimeHostMode();
        inline bool gEnableImGui = false;
        inline bool gEnableEditorUI = false;
        inline bool gEnablePortableObjectTools = false;
        inline std::string gRuntimeStartupSceneGuid{};
        inline std::vector<std::string> gRuntimeExportedSceneGuids{};
        inline D3D12_CPU_DESCRIPTOR_HANDLE gImGuiFontSrvCpu{};
        inline D3D12_GPU_DESCRIPTOR_HANDLE gImGuiFontSrvGpu{};
        inline bool gGpuFrameReady = false;
        inline bool gPresentationPrepared = false;
        inline bool gVfxFrameEnded = false;
        inline bool gHasPendingWindowResize = false;
        inline int gPendingWindowWidth = 0;
        inline int gPendingWindowHeight = 0;
        inline int gLogicalScreenWidth =
            PRESENTATION::kDefaultLogicalWidth;
        inline int gLogicalScreenHeight =
            PRESENTATION::kDefaultLogicalHeight;
        inline bool gEditorGameViewportVisible = false;
        inline int gEditorGameViewportWidth = 0;
        inline int gEditorGameViewportHeight = 0;
        inline std::wstring gRuntimeWindowBaseTitle{};
        inline double gPreviewTelemetrySeconds = 0.0;
        inline uint64_t gPreviewTelemetryRenderFrames = 0;
        inline uint64_t gPreviewTelemetryPresentedFrames = 0;

        inline RuntimeHostMode GetRuntimeHostMode() { return gRuntimeHostMode; }
        inline INPUT::InputService& GetInputService() { return gInputService; }
        inline const INPUT::InputSnapshot& GetInputSnapshot() {
            return gInputService.GetSnapshot();
        }
        inline bool IsEditorHost() { return IsEditorHostMode(gRuntimeHostMode); }
        inline bool IsStandaloneGameHost() { return IsStandaloneGameHostMode(gRuntimeHostMode); }
        inline bool IsInProcessGamePresentationActive() {
            return gGamePresentationController.IsActive();
        }
        inline bool EndInProcessGamePresentation();
        inline bool IsGamePresentationActive() {
            return IsStandaloneGameHost() || IsInProcessGamePresentationActive();
        }
        inline bool IsImGuiEnabled() { return gEnableImGui; }
        inline bool IsEditorUIEnabled() { return gEnableImGui && gEnableEditorUI; }
        inline bool ShouldProduceEditorUiFrame() {
            return IsEditorHost() &&
                IsEditorUIEnabled() &&
                !IsInProcessGamePresentationActive();
        }
        inline bool ArePortableObjectToolsEnabled() { return gEnableImGui && gEnablePortableObjectTools; }
        inline const std::string& GetRuntimeStartupSceneGuid() { return gRuntimeStartupSceneGuid; }
        inline const std::vector<std::string>& GetRuntimeExportedSceneGuids() { return gRuntimeExportedSceneGuids; }
        inline bool IsRuntimeSceneGuidAllowed(const std::string& sceneGuid) {
            if (gRuntimeExportedSceneGuids.empty()) {
                return true;
            }
            return std::find(
                gRuntimeExportedSceneGuids.begin(),
                gRuntimeExportedSceneGuids.end(),
                sceneGuid) != gRuntimeExportedSceneGuids.end();
        }
        inline void SetEditorUIEnabled(bool enabled) { gEnableEditorUI = IsEditorHost() && enabled; }
        inline void SetEditorGameViewportSize(int width, int height, bool visible) {
            gEditorGameViewportVisible = visible && width > 0 && height > 0;
            gEditorGameViewportWidth = gEditorGameViewportVisible ? width : 0;
            gEditorGameViewportHeight = gEditorGameViewportVisible ? height : 0;
        }

        inline void UpdateGpuContexts() {
            gCtx = gCore.BuildContext();
            RENDER3D::UPSCALING::UpdateStreamlineContext(gCtx);
            DXTEX::DxTextureManager::UpdateContext(gCtx);
            RENDER3D::UpdateRenderResourceDescriptorPoolContext(gCtx);
            RENDER3D::UpdateClusterGeometryResourceContext(gCtx);
            DX::DxRenderer::UpdateContext(gCtx);
            POST::PostSystem::UpdateContext(gCtx);
            HIKARI::VFX::UpdateContext(gCtx);
        }

        inline void QueueWindowResize(int width, int height) {
            if (width <= 0 || height <= 0) {
                return;
            }
            gPendingWindowWidth = width;
            gPendingWindowHeight = height;
            gHasPendingWindowResize = true;
        }

        inline void ClearPendingWindowResize() {
            gHasPendingWindowResize = false;
            gPendingWindowWidth = 0;
            gPendingWindowHeight = 0;
        }

        inline bool ApplyPendingWindowResize() {
            if (!gHasPendingWindowResize) {
                return true;
            }

            const int width = gPendingWindowWidth;
            const int height = gPendingWindowHeight;

            if (width <= 0 || height <= 0) {
                ClearPendingWindowResize();
                return true;
            }
            if (width == static_cast<int>(gCtx.backBufferWidth) &&
                height == static_cast<int>(gCtx.backBufferHeight)) {
                ClearPendingWindowResize();
                return true;
            }

            const bool deactivateFrameGeneration =
                IsInProcessGamePresentationActive() &&
                gGamePresentationController.IsGameSurfaceActive() &&
                RENDER3D::UPSCALING::
                    IsStreamlineFrameGenerationFeatureLoaded();
            if (deactivateFrameGeneration) {
                if (!gCore.WaitForIdle()) {
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
            const bool resized = gCore.Resize(width, height);
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

        inline bool ApplyWindowPresentationSettings() {
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

        struct FrameSceneResolutionPlan {
            RENDER3D::RenderResolution render{};
            RENDER3D::RenderResolution output{};
            RENDER3D::UPSCALING::StreamlineDlssMode streamlineMode =
                RENDER3D::UPSCALING::StreamlineDlssMode::Off;
        };

        enum class FramePresentationDestination {
            BackBuffer,
            EditorViewport
        };

        inline FrameSceneResolutionPlan ResolveFrameSceneResolutionPlan() {
            const RENDER3D::RenderQualitySettings& settings =
                RENDER3D::GetRenderQualitySettings();

            int viewportWidth = static_cast<int>(gCtx.backBufferWidth);
            int viewportHeight = static_cast<int>(gCtx.backBufferHeight);
            if (viewportWidth <= 0 || viewportHeight <= 0) {
                viewportWidth = gWindow.Width();
                viewportHeight = gWindow.Height();
            }
#if defined(HIKARI_WITH_EDITOR)
            if (ShouldProduceEditorUiFrame() &&
                gEditorGameViewportVisible &&
                gEditorGameViewportWidth > 0 &&
                gEditorGameViewportHeight > 0) {
                viewportWidth = gEditorGameViewportWidth;
                viewportHeight = gEditorGameViewportHeight;
            }
#endif

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

        inline void ApplyFrameSceneCaptureSize() {
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

        inline void ConfigureEditorImGuiContext() {
#if defined(HIKARI_ENABLE_IMGUI)
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            io.ConfigDockingWithShift = false;
            io.ConfigDockingTransparentPayload = true;
            io.ConfigViewportsNoAutoMerge = false;
            io.ConfigViewportsNoTaskBarIcon = false;
#if defined(HIKARI_WITH_EDITOR)
            (void)EDITOR::EnsureEditorFonts();
            EDITOR::ApplyEditorStyle();
#endif
#endif
        }

        inline void InitializeImGuiBackend() {
#if !defined(HIKARI_ENABLE_IMGUI)
            return;
#else
            if (gImGuiBackendInitialized) {
                return;
            }

            auto* device = gCtx.device;
            auto* srvHeap = gCtx.srvHeap;
            if (!device || !srvHeap || !gWindow.GetHWND()) {
                return;
            }

            const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            const UINT imguiFontSrvIndex =
                GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::ImGuiFont);
            gImGuiFontSrvCpu =
                GFX::DESCRIPTOR::CpuAt(srvHeap, descriptorSize, imguiFontSrvIndex);
            gImGuiFontSrvGpu =
                GFX::DESCRIPTOR::GpuAt(srvHeap, descriptorSize, imguiFontSrvIndex);

            if (!ImGui_ImplWin32_Init(gWindow.GetHWND())) {
                HIKARI_LOG_ERROR("ImGui_ImplWin32_Init failed.");
                return;
            }

            if (!ImGui_ImplDX12_Init(
                device,
                static_cast<int>(GFX::kFrameResourceCount),
                DXGI_FORMAT_R8G8B8A8_UNORM,
                srvHeap,
                gImGuiFontSrvCpu,
                gImGuiFontSrvGpu)) {
                HIKARI_LOG_ERROR("ImGui_ImplDX12_Init failed.");
                ImGui_ImplWin32_Shutdown();
                return;
            }

            gImGuiBackendInitialized = true;
            HIKARI_LOG_INFO("ImGui backend initialized.");
#endif
        }

        inline bool Initialize(const char* title, const BootstrapConfig& cfg = {}) {
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
            gEnablePortableObjectTools =
                AllowsPortableObjectTools(gRuntimeHostMode) && cfg.enablePortableObjectTools;
            gRuntimeStartupSceneGuid = cfg.startupSceneGuid;
            gRuntimeExportedSceneGuids = cfg.exportedSceneGuids;
            {
                std::ostringstream oss;
                oss << "Runtime host mode=" << RuntimeHostModeName(gRuntimeHostMode)
                    << " imgui=" << (gEnableImGui ? "true" : "false")
                    << " editorUI=" << (gEnableEditorUI ? "true" : "false")
                    << " portableObjectTools=" << (gEnablePortableObjectTools ? "true" : "false")
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

            if (gEnableImGui && !gImGuiInitialized) {
#if defined(HIKARI_ENABLE_IMGUI)
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                ConfigureEditorImGuiContext();

                ImGuiIO& io = ImGui::GetIO();
                if (io.Fonts && io.Fonts->Fonts.empty()) {
#if defined(HIKARI_WITH_EDITOR)
                    (void)EDITOR::EnsureEditorFonts();
#else
                    io.Fonts->AddFontDefault();
                    io.Fonts->Build();
#endif
                }
                gImGuiInitialized = true;
                HIKARI_LOG_INFO("ImGui context initialized.");
#else
                gEnableImGui = false;
                gEnableEditorUI = false;
                gEnablePortableObjectTools = false;
#endif
            }
            if (gEnableImGui) {
                InitializeImGuiBackend();
            }
            HIKARI_LOG_INFO("HIKARI boot completed.");
            return true;
        }

        inline void FinalizeAll() {
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
            if (gImGuiInitialized) {
#if defined(HIKARI_ENABLE_IMGUI)
                if (gImGuiBackendInitialized) {
                    ImGui_ImplDX12_Shutdown();
                    ImGui_ImplWin32_Shutdown();
                    gImGuiBackendInitialized = false;
                    HIKARI_LOG_INFO("ImGui backend shutdown.");
                }
#if defined(HIKARI_WITH_EDITOR)
                EDITOR::ResetEditorFonts();
#endif
                ImGui::DestroyContext();
#endif
                gImGuiInitialized = false;
                HIKARI_LOG_INFO("ImGui shutdown.");
            }
            gImGuiFrameBegun = false;
#if defined(HIKARI_WITH_EDITOR)
            if (!gCore.WaitForIdle()) {
                HIKARI_LOG_ERROR(
                    "GPU idle wait failed before editor interactive view shutdown.");
            }
            HIKARI::RENDER3D::EDITORVIEW::Shutdown();
            HIKARI_LOG_INFO("Editor interactive view renderer shutdown.");
#endif
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

        inline bool PumpMessages() {
            return gWindow.PumpMessages();
        }

        inline bool BeginInProcessGamePresentation() {
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
            if (!gCore.WaitForIdle()) {
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
                    gCore,
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
                    (void)gGamePresentationController.ReleaseGameSurface(gCore);
                }
                (void)RENDER3D::UPSCALING::
                    SetStreamlineFrameGenerationFeatureLoaded(false);
                (void)gGamePresentationController.RestoreEditorSurface(
                    gCore, gWindow);
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

        inline bool BeginInProcessGamePresentationStop() {
            if (!IsInProcessGamePresentationActive()) {
                return true;
            }
            if (gGpuFrameReady) {
                return false;
            }
            gGamePresentationController.MarkStopping();
            gInputService.SetMouseCaptureMode(
                INPUT::MouseCaptureMode::Free);
            if (!gCore.WaitForIdle()) {
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

        inline bool DrainInProcessGamePresentation() {
            if (!IsInProcessGamePresentationActive() ||
                gGamePresentationController.IsGameSurfaceReleased()) {
                return true;
            }
            if (gGpuFrameReady || !gCore.WaitForIdle()) {
                HIKARI_LOG_ERROR(
                    "Could not drain GPU work before releasing the game presentation surface.");
                return false;
            }
            return gGamePresentationController.ReleaseGameSurface(gCore);
        }

        inline bool RestoreEditorPresentation() {
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
                    gCore, gWindow);
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

        inline bool EndInProcessGamePresentation() {
            if (!BeginInProcessGamePresentationStop()) {
                return false;
            }
            if (!DrainInProcessGamePresentation()) {
                return false;
            }
            return RestoreEditorPresentation();
        }

        inline bool ConsumeInProcessGameCloseRequest() {
            return gGamePresentationController.HasCloseRequest();
        }

        inline void UpdateGamePresentationPerformanceTitle() {
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

        inline bool BeginFrame(const BootstrapConfig& cfg = {}) {
            (void)cfg;
            gGpuFrameReady = false;
            gPresentationPrepared = false;
            gVfxFrameEnded = false;
            GFX::PIX::ScopedCpuEvent pixCpuFrame(GFX::PIX::kColorFrame, "Services.BeginFrame");
            const FrameContext& frame = HIKARI::TIME::BeginFrame();
            CPU_PROFILE::BeginFrame(frame.frameIndex);
            CPU_PROFILE::ScopedCpuTimer cpuBeginFrame(
                CPU_PROFILE::Pass::ServicesBeginFrame);
            if (!ApplyPendingWindowResize()) {
                return false;
            }
            UpdateGpuContexts();

            (void)RENDER3D::UPSCALING::BeginStreamlineFrame(frame.frameIndex);
            (void)RENDER3D::UPSCALING::BeginStreamlineReflexFrame();

            if (!gCore.BeginFrame(0.05f, 0.08f, 0.12f, 1.0f)) {
                HIKARI_LOG_ERROR("D3D12 BeginFrame failed; skipping frame.");
                return false;
            }
            gGpuFrameReady = true;

            HIKARI::RENDERER::BeginFrame();
            ApplyFrameSceneCaptureSize();
            HIKARI::POST::PostSystem::UpdateCommonParams(frame.gameDt);
#if defined(HIKARI_WITH_EDITOR)
            if (!ShouldProduceEditorUiFrame()) {
                HIKARI::EDITOR::ClearGameViewportInputRect();
            }
#endif
            HIKARI::POST::PostSystem::BeginSceneCapture();

            DX::DxRenderer::BeginFrame();
            PLATFORM::Win32Window* inputWindow =
                gGamePresentationController.GetGameWindow();
            gInputService.SetExternalMouseWheel(
                inputWindow != nullptr
                    ? inputWindow->ConsumeMouseWheelDelta()
                    : gWindow.ConsumeMouseWheelDelta());
            gInputService.Update(frame.unscaledDt);
            HIKARI::VFX::BeginFrame(frame.gameDt);
            if (gEnableImGui &&
                gImGuiInitialized &&
                ShouldProduceEditorUiFrame()) {
#if defined(HIKARI_ENABLE_IMGUI)
                if (!ImGui::GetCurrentContext()) {
                    ImGui::CreateContext();
                    ConfigureEditorImGuiContext();
                }

                ConfigureEditorImGuiContext();

                if (!gImGuiBackendInitialized) {
                    InitializeImGuiBackend();
                }

                if (gImGuiBackendInitialized) {
                    ImGui_ImplDX12_NewFrame();
                    ImGui_ImplWin32_NewFrame();
                }

                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = ImVec2(static_cast<float>(gWindow.Width()), static_cast<float>(gWindow.Height()));
                io.DeltaTime = (frame.unscaledDt > 0.0f) ? frame.unscaledDt : (1.0f / 60.0f);
                if (io.Fonts && io.Fonts->Fonts.empty()) {
#if defined(HIKARI_WITH_EDITOR)
                    (void)EDITOR::EnsureEditorFonts();
#else
                    io.Fonts->AddFontDefault();
                    io.Fonts->Build();
#endif
                }
                ImGui::NewFrame();
                gImGuiFrameBegun = true;
#endif
            }
            HIKARI::CAMERA::Update(
                frame.gameDt,
                &gInputService.GetSnapshot());
            return true;
        }

        inline bool PrepareFramePresentation(
            FramePresentationDestination destination =
                FramePresentationDestination::EditorViewport) {
            if (!gGpuFrameReady) {
                return false;
            }
            if (gPresentationPrepared) {
                return true;
            }

            if (!gVfxFrameEnded) {
                HIKARI::VFX::EndFrame();
                gVfxFrameEnded = true;
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
                return false;
            }

            {
                GFX::PIX::ScopedGpuEvent pixScene(
                    gCtx.cmdList,
                    GFX::PIX::kColorRender,
                    "Scene Layers");
                GFX::GPU_PROFILE::ScopedGpuTimer gpuScene(
                    gCtx.cmdList,
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
                    gCtx.cmdList,
                    GFX::PIX::kColorPost,
                    "PostSystem");
                GFX::GPU_PROFILE::ScopedGpuTimer gpuPost(
                    gCtx.cmdList,
                    GFX::GPU_PROFILE::Pass::PostResolve);
                if (!HIKARI::POST::PostSystem::PrepareGameUiComposition(
                        editorViewport,
                        gLogicalScreenWidth,
                        gLogicalScreenHeight)) {
                    HIKARI_LOG_ERROR("Game UI composition preparation failed.");
                    return false;
                }
            }

            {
                CPU_PROFILE::ScopedCpuTimer cpuUi(
                    CPU_PROFILE::Pass::UiLayers);
                GFX::PIX::ScopedGpuEvent pixUi(
                    gCtx.cmdList,
                    GFX::PIX::kColorEditor,
                    "Game UI and Debug Layers");
                GFX::GPU_PROFILE::ScopedGpuTimer gpuUi(
                    gCtx.cmdList,
                    GFX::GPU_PROFILE::Pass::UiLayers);
                HIKARI::RENDERER::RenderLayerRange(
                    HIKARI::RENDERER::RenderLayer::UI,
                    HIKARI::RENDERER::RenderLayer::Debug,
                    true);
            }

            if (!HIKARI::POST::PostSystem::FinalizeGameUiComposition(
                    editorViewport)) {
                HIKARI_LOG_ERROR("Game UI composition finalization failed.");
                return false;
            }

            const HIKARI::POST::PresentationFrameResources& presentation =
                HIKARI::POST::PostSystem::GetPresentationFrameResources();
            RENDER3D::UPSCALING::SynchronizeStreamlineFrameGenerationPolicy(
                RENDER3D::GetRenderQualitySettings());
            if (presentation.HasFrameGenerationInputs()) {
                const RENDER3D::TEMPORAL::TemporalInputs temporal =
                    RENDER3D::TEMPORAL::GetCurrentTemporalInputs();
                (void)RENDER3D::UPSCALING::SubmitStreamlineFrameGenerationInputs(
                    temporal,
                    *presentation.hudlessColor,
                    *presentation.uiColorAndAlpha,
                    presentation.frameIndex,
                    IsGamePresentationActive());
            }

            gPresentationPrepared = true;
            return true;
        }

        inline bool EndFrame() {
            if (!gGpuFrameReady) {
                return false;
            }
            GFX::PIX::ScopedCpuEvent pixCpuFrame(GFX::PIX::kColorFrame, "Services.EndFrame");
            if (!PrepareFramePresentation()) {
                HIKARI::RENDERER::ClearSubmittedCommands();
            }
            if (gEnableImGui && gImGuiInitialized && gImGuiFrameBegun) {
#if defined(HIKARI_ENABLE_IMGUI)
                ImGui::Render();
                gImGuiFrameBegun = false;
#endif
            }
            if (gEnableImGui &&
                gImGuiInitialized &&
                gImGuiBackendInitialized &&
                ShouldProduceEditorUiFrame()) {
#if defined(HIKARI_ENABLE_IMGUI)
                CPU_PROFILE::ScopedCpuTimer cpuImGui(
                    CPU_PROFILE::Pass::ImGui);
                GFX::PIX::ScopedGpuEvent pixImGui(gCtx.cmdList, GFX::PIX::kColorEditor, "ImGui");
                GFX::GPU_PROFILE::ScopedGpuTimer gpuImGui(
                    gCtx.cmdList,
                    GFX::GPU_PROFILE::Pass::ImGui);
                auto* cmd = gCtx.cmdList;
                ID3D12DescriptorHeap* heaps[] = { gCtx.srvHeap };
                cmd->SetDescriptorHeaps(1, heaps);
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd);

                ImGuiIO& io = ImGui::GetIO();
                if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
                    ImGui::UpdatePlatformWindows();
                    ImGui::RenderPlatformWindowsDefault();
                }
#endif
            }

            gCore.SetVSyncEnabled(RENDER3D::GetRenderQualitySettings().vSync);
            if (!gCore.EndFrame()) {
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
