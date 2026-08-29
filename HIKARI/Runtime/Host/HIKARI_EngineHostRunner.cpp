#include "Runtime/Host/HIKARI_EngineHostRunner.h"

#include <filesystem>

#include <Windows.h>

#include "Assets/Cooking/HIKARI_CookedContentManifest.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/HIKARI_TimeService.h"
#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Input/Runtime/HIKARI_InputActionIds.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Project/HIKARI_ProjectLaunch.h"
#include "Render2D/HIKARI_Camera.h"
#include "Render2D/HIKARI_Renderer.h"
#include "Render2D/HIKARI_SpineActor.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Upscaling/HIKARI_StreamlineReflex.h"
#include "Runtime/App/HIKARI_IHostApplication.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "Runtime/Host/HIKARI_HostBootstrapConfig.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"

namespace HIKARI::RUNTIME {

    namespace {

        HostBootstrapConfig ResolveBootstrapConfig(
            EngineHostProduct product,
            const RuntimeLaunchConfig& runtimeConfig) {

            HostBootstrapConfig config{};
            if (product == EngineHostProduct::Editor) {
                config.hostMode = RuntimeHostMode::Editor;
                config.enableImGui = true;
                config.enableEditorUI = true;
            }

            ApplyRuntimeLaunchConfig(runtimeConfig, config);
            if (product == EngineHostProduct::Editor) {
                config.hostMode = RuntimeHostMode::Editor;
            }
            else {
                if (!IsStandaloneGameHostMode(config.hostMode)) {
                    config.hostMode = RuntimeHostMode::Game;
                }
                config.enableImGui = false;
                config.enableEditorUI = false;
            }
            return config;
        }

        const char* ProjectContextSourceName(
            ProjectContextSource source) {

            switch (source) {
            case ProjectContextSource::Descriptor:
                return "Project.hikari";
            case ProjectContextSource::
                    RuntimeConfigCompatibility:
                return "runtime_config compatibility";
            case ProjectContextSource::
                    WorkingDirectoryCompatibility:
                return "working-directory compatibility";
            }
            return "unknown";
        }

        // ウィンドウ生成前に DPI 認識を有効化する。
        // 未設定だとスケーリング 150% などで OS に仮想化され、
        // 最大化するまで描画とマウス座標がずれたままになる。
        void EnableProcessDpiAwareness() {

            if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
                using SetContextFn =
                    DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
                if (auto setContext =
                        reinterpret_cast<SetContextFn>(GetProcAddress(
                            user32, "SetProcessDpiAwarenessContext"))) {
                    if (setContext(
                            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE) {
                        return;
                    }
                }
            }
            if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
                using SetAwarenessFn = HRESULT(WINAPI*)(int);
                // PROCESS_PER_MONITOR_DPI_AWARE
                if (auto setAwareness =
                        reinterpret_cast<SetAwarenessFn>(GetProcAddress(
                            shcore, "SetProcessDpiAwareness"))) {
                    if (SUCCEEDED(setAwareness(2))) {
                        return;
                    }
                }
            }
            SetProcessDPIAware();
        }

        int ReportLaunchFailure(
            const std::string& message) {

            const std::string displayMessage =
                "HIKARI could not start.\n\n" +
                message;
            OutputDebugStringA(displayMessage.c_str());
            MessageBoxA(
                nullptr,
                displayMessage.c_str(),
                "HIKARI Launch Error",
                MB_OK | MB_ICONERROR);
            return -2;
        }

    } // namespace

    int RunEngineHost(
        IHostApplication& application,
        const EngineHostRunOptions& options) {

        EnableProcessDpiAwareness();

        std::string engineContentMessage{};
        if (!CONTENT::OpenEngineContentForLaunch(
                &engineContentMessage)) {
            return ReportLaunchFailure(
                engineContentMessage);
        }

        const RuntimeLaunchConfig runtimeConfig =
            options.loadRuntimeConfig
                ? LoadRuntimeLaunchConfig()
                : RuntimeLaunchConfig{};
        if (options.projectPolicy ==
            EngineHostProjectPolicy::Required) {
            ProjectLaunchOptions projectLaunchOptions{};
            projectLaunchOptions.explicitProjectPath =
                runtimeConfig.requestedProjectPath;
            if (runtimeConfig.loaded &&
                !runtimeConfig.projectRoot.empty()) {
                projectLaunchOptions.runtimeConfigProjectRoot =
                    runtimeConfig.projectRoot;
            }

            std::string projectMessage{};
            if (!OpenProjectForLaunch(
                    projectLaunchOptions,
                    &projectMessage) ||
                !ApplyProjectWorkingDirectory(
                    &projectMessage)) {
                GetProjectContext().Close();
                CONTENT::GetEngineContentContext().Close();
                return ReportLaunchFailure(
                    projectMessage);
            }
        }
        else {
            GetProjectContext().Close();
        }

        const HostBootstrapConfig servicesConfig =
            ResolveBootstrapConfig(options.product, runtimeConfig);
        const ProjectContext& projectContext =
            GetProjectContext();

#if defined(HIKARI_SHIPPING)
        if (projectContext.IsOpen()) {
            const std::filesystem::path cookedManifestPath =
                projectContext.Resolve(
                    ASSETS::COOKING::
                        kCookedContentManifestFilename);
            std::error_code cookedManifestError{};
            if (!std::filesystem::is_regular_file(
                    cookedManifestPath,
                    cookedManifestError) ||
                cookedManifestError) {
                GetProjectContext().Close();
                CONTENT::GetEngineContentContext().Close();
                return ReportLaunchFailure(
                    "Shipping builds require cooked content. Missing " +
                    cookedManifestPath.generic_string());
            }
        }
#endif

        const char* resolvedWindowTitle = options.windowTitle;
        if (runtimeConfig.windowTitle &&
            !runtimeConfig.windowTitle->empty()) {
            resolvedWindowTitle = runtimeConfig.windowTitle->c_str();
        }
        if (!SERVICES::Initialize(
                resolvedWindowTitle,
                servicesConfig)) {
            GetProjectContext().Close();
            CONTENT::GetEngineContentContext().Close();
            return -1;
        }
        const CONTENT::EngineContentContext& engineContent =
            CONTENT::GetEngineContentContext();
        HIKARI_LOG_INFO(
            "[EngineContent] opened root=" +
            engineContent.GetRoot().generic_string());
        if (projectContext.IsOpen()) {
            HIKARI_LOG_INFO(
                "[Project] opened '" +
                projectContext.GetDescriptor().name +
                "' root=" +
                projectContext.GetRoot().generic_string() +
                " source=" +
                ProjectContextSourceName(
                    projectContext.GetSource()));
        }
        else {
            HIKARI_LOG_INFO(
                "[Project] no active project; launcher mode");
        }
        if (!SERVICES::IsEditorHost()) {
            (void)SERVICES::ApplyWindowPresentationSettings();
        }

        MATH::RunMathConventionSelfCheck();
        SpineActor openingAnimation;
        const std::filesystem::path openingRoot =
            IsExportedGameHostMode(
                servicesConfig.hostMode)
                ? std::filesystem::path(
                    "RuntimePayload/Spine")
                : std::filesystem::path(
                    "Assets/Spine");
        const std::filesystem::path openingAtlas =
            projectContext.IsOpen()
                ? projectContext.Resolve(
                    openingRoot / "op.atlas")
                : std::filesystem::path{};
        const std::filesystem::path openingSkeleton =
            projectContext.IsOpen()
                ? projectContext.Resolve(
                    openingRoot / "op.json")
                : std::filesystem::path{};
        std::error_code openingError{};
        const bool hasOpeningAssets =
            std::filesystem::exists(openingAtlas, openingError) &&
            std::filesystem::exists(openingSkeleton, openingError);
        const bool openingAnimationActive =
            hasOpeningAssets &&
            openingAnimation.Load(
                openingAtlas.generic_string(),
                openingSkeleton.generic_string());
        if (openingAnimationActive) {
            openingAnimation.enableCamera = false;
            openingAnimation.SetAnimation("op", false);
        }
        else {
            HIKARI_LOG_INFO(
                "[Host] optional project opening animation is unavailable; "
                "starting application immediately");
        }

        if (!application.Initialize()) {
            SERVICES::FinalizeAll();
            GetProjectContext().Close();
            CONTENT::GetEngineContentContext().Close();
            return -1;
        }

        while (SERVICES::PumpMessages()) {
            application.UpdateHostSessions();
            if (application.ShouldExitHost()) {
                break;
            }
            if (application.IsHostTransitioning()) {
                Sleep(1);
                continue;
            }
            if (!SERVICES::BeginFrame(servicesConfig)) {
                break;
            }
            const FrameContext& frame =
                TIME::GetFrameContext();

            bool renderHostUi = false;
            if (openingAnimationActive &&
                !openingAnimation.IsAnimationFinished("op")) {
                {
                    CPU_PROFILE::ScopedCpuTimer cpuUpdate(
                        CPU_PROFILE::Pass::AppUpdate);
                    openingAnimation.Update(frame.gameDt);
                }
                RENDER3D::UPSCALING::
                    EndStreamlineReflexSimulation();
                {
                    CPU_PROFILE::ScopedCpuTimer cpuRender(
                        CPU_PROFILE::Pass::AppRender);
                    constexpr float kOpeningCanvasWidth = 1280.0f;
                    constexpr float kOpeningCanvasHeight = 720.0f;
                    const float screenWidth =
                        static_cast<float>(
                            CAMERA::GetScreenWidth());
                    const float screenHeight =
                        static_cast<float>(
                            CAMERA::GetScreenHeight());
                    if (screenWidth > 0.0f &&
                        screenHeight > 0.0f) {
                        const float scaleX =
                            screenWidth / kOpeningCanvasWidth;
                        const float scaleY =
                            screenHeight / kOpeningCanvasHeight;
                        const float fitScale =
                            scaleX < scaleY
                            ? scaleX
                            : scaleY;
                        openingAnimation.transform.position = {
                            screenWidth * 0.5f,
                            screenHeight * 0.5f };
                        openingAnimation.transform.scale = {
                            fitScale,
                            fitScale };
                    }
                    const RENDERER::RenderLayer previousLayer =
                        RENDERER::GetCurrentLayer();
                    RENDERER::SetCurrentLayer(
                        RENDERER::RenderLayer::UI);
                    openingAnimation.Draw();
                    RENDERER::SetCurrentLayer(previousLayer);
                }
            }
            else {
                {
                    CPU_PROFILE::ScopedCpuTimer cpuUpdate(
                        CPU_PROFILE::Pass::AppUpdate);
                    application.Update(frame.gameDt);
                }
                RENDER3D::UPSCALING::
                    EndStreamlineReflexSimulation();
                {
                    CPU_PROFILE::ScopedCpuTimer cpuRender(
                        CPU_PROFILE::Pass::AppRender);
                    application.Render();
                }
                renderHostUi =
                    SERVICES::ShouldProduceEditorUiFrame();
            }

            const SERVICES::FramePresentationDestination presentation =
                renderHostUi
                ? SERVICES::FramePresentationDestination::EditorViewport
                : SERVICES::FramePresentationDestination::BackBuffer;
            (void)SERVICES::PrepareFramePresentation(presentation);
            if (renderHostUi) {
                CPU_PROFILE::ScopedCpuTimer cpuImGui(
                    CPU_PROFILE::Pass::AppImGui);
                application.RenderHostUi();
            }

            const INPUT::InputSnapshot& input =
                SERVICES::GetInputSnapshot();
            if (SERVICES::ShouldProduceEditorUiFrame() &&
                input.IsPressed(
                    INPUT::ActionIds::ToggleEditorUI)) {
                SERVICES::SetEditorUIEnabled(
                    !SERVICES::IsEditorUIEnabled());
            }

            if (!SERVICES::EndFrame()) {
                break;
            }
            if (application.ShouldExitHost()) {
                break;
            }

            if (application.IsWindowedHostSessionRunning() &&
                input.IsPressed(INPUT::ActionIds::StopPlay)) {
                application.RequestHostStop();
            }
            if (input.IsPressed(
                    INPUT::ActionIds::CloseProgram)) {
                break;
            }
        }

        application.Shutdown();
        SERVICES::FinalizeAll();
        GetProjectContext().Close();
        CONTENT::GetEngineContentContext().Close();
        return 0;
    }

} // namespace HIKARI::RUNTIME
