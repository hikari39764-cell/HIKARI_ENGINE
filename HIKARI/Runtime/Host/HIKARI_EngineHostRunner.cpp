#include "Runtime/Host/HIKARI_EngineHostRunner.h"

#include <Windows.h>

#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Input/Runtime/HIKARI_InputActionIds.h"
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

    } // namespace

    int RunEngineHost(
        IHostApplication& application,
        const EngineHostRunOptions& options) {

        const RuntimeLaunchConfig runtimeConfig =
            LoadRuntimeLaunchConfig();
        ApplyRuntimeWorkingDirectory(runtimeConfig);
        const HostBootstrapConfig servicesConfig =
            ResolveBootstrapConfig(options.product, runtimeConfig);

        if (!SERVICES::Initialize(
                options.windowTitle,
                servicesConfig)) {
            return -1;
        }
        if (!SERVICES::IsEditorHost()) {
            (void)SERVICES::ApplyWindowPresentationSettings();
        }

        MATH::RunMathConventionSelfCheck();
        SpineActor openingAnimation;
        openingAnimation.Load(
            "./Assets/Spine/op.atlas",
            "./Assets/Spine/op.json");
        openingAnimation.enableCamera = false;
        openingAnimation.SetAnimation("op", false);

        if (!application.Initialize()) {
            SERVICES::FinalizeAll();
            return -1;
        }

        while (SERVICES::PumpMessages()) {
            application.UpdateHostSessions();
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
            if (!openingAnimation.IsAnimationFinished("op")) {
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
        return 0;
    }

} // namespace HIKARI::RUNTIME
