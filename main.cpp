#include "HIKARI/HIKARI_Services.h"
#include "HIKARI/App/HIKARI_EngineApp.h"
#include "HIKARI/Core/HIKARI_TimeService.h"
#include "HIKARI/Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "HIKARI/Render2D/HIKARI_Camera.h"
#include "HIKARI/Render2D/HIKARI_Renderer.h"
#include "HIKARI/Render2D/HIKARI_SpineActor.h"
#include "HIKARI/Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "HIKARI/Input/Runtime/HIKARI_InputActionIds.h"

const char kWindowTitle[] = "HIKARI_Ver1.3";

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	HIKARI::SERVICES::BootstrapConfig servicesCfg{};
	const HIKARI::RuntimeLaunchConfig runtimeCfg = HIKARI::LoadRuntimeLaunchConfig();
	HIKARI::ApplyRuntimeWorkingDirectory(runtimeCfg);
	HIKARI::ApplyRuntimeLaunchConfig(runtimeCfg, servicesCfg);

	if (!HIKARI::SERVICES::Initialize(kWindowTitle, servicesCfg)) {
		return -1;
	}
	if (!HIKARI::SERVICES::IsEditorHost()) {
		(void)HIKARI::SERVICES::ApplyWindowPresentationSettings();
	}

	HIKARI::MATH::RunMathConventionSelfCheck();
	HIKARI::SpineActor op;
	op.Load("./Assets/Spine/op.atlas", "./Assets/Spine/op.json");
	op.enableCamera = false;
	op.SetAnimation("op", false);

	HIKARI::EngineApp app;
	if (!app.Initialize()) {
		HIKARI::SERVICES::FinalizeAll();
		return -1;
	}

	while (HIKARI::SERVICES::PumpMessages()) {
		app.UpdatePlaySessions();
		if (app.IsPlayTransitioning()) {
			Sleep(1);
			continue;
		}
		if (!HIKARI::SERVICES::BeginFrame(servicesCfg)) {
			break;
		}
		const HIKARI::FrameContext& frame = HIKARI::TIME::GetFrameContext();

		bool renderEditorUi = false;
		if (!op.IsAnimationFinished("op"))
		{
			{
				HIKARI::CPU_PROFILE::ScopedCpuTimer cpuUpdate(
					HIKARI::CPU_PROFILE::Pass::AppUpdate);
				op.Update(frame.gameDt);
			}
			HIKARI::RENDER3D::UPSCALING::EndStreamlineReflexSimulation();
			{
				HIKARI::CPU_PROFILE::ScopedCpuTimer cpuRender(
					HIKARI::CPU_PROFILE::Pass::AppRender);
				constexpr float kOpCanvasWidth = 1280.0f;
				constexpr float kOpCanvasHeight = 720.0f;
				const float screenWidth = static_cast<float>(
					HIKARI::CAMERA::GetScreenWidth());
				const float screenHeight = static_cast<float>(
					HIKARI::CAMERA::GetScreenHeight());
				if (screenWidth > 0.0f && screenHeight > 0.0f) {
					const float scaleX = screenWidth / kOpCanvasWidth;
					const float scaleY = screenHeight / kOpCanvasHeight;
					const float fitScale = scaleX < scaleY ? scaleX : scaleY;
					op.transform.position = {
						screenWidth * 0.5f,
						screenHeight * 0.5f };
					op.transform.scale = { fitScale, fitScale };
				}
				const HIKARI::RENDERER::RenderLayer previousLayer =
					HIKARI::RENDERER::GetCurrentLayer();
				HIKARI::RENDERER::SetCurrentLayer(
					HIKARI::RENDERER::RenderLayer::UI);
				op.Draw();
				HIKARI::RENDERER::SetCurrentLayer(previousLayer);
			}
		} else {
			{
				HIKARI::CPU_PROFILE::ScopedCpuTimer cpuUpdate(
					HIKARI::CPU_PROFILE::Pass::AppUpdate);
				app.Update(frame.gameDt);
			}
			HIKARI::RENDER3D::UPSCALING::EndStreamlineReflexSimulation();
			{
				HIKARI::CPU_PROFILE::ScopedCpuTimer cpuRender(
					HIKARI::CPU_PROFILE::Pass::AppRender);
				app.Render();
			}
			renderEditorUi = HIKARI::SERVICES::ShouldProduceEditorUiFrame();
		}

		const HIKARI::SERVICES::FramePresentationDestination presentation =
			renderEditorUi
			? HIKARI::SERVICES::FramePresentationDestination::EditorViewport
			: HIKARI::SERVICES::FramePresentationDestination::BackBuffer;
		(void)HIKARI::SERVICES::PrepareFramePresentation(presentation);
		if (renderEditorUi) {
			HIKARI::CPU_PROFILE::ScopedCpuTimer cpuImGui(
				HIKARI::CPU_PROFILE::Pass::AppImGui);
			app.RenderImGui();
		}


		const HIKARI::INPUT::InputSnapshot& input =
			HIKARI::SERVICES::GetInputSnapshot();
		if (HIKARI::SERVICES::ShouldProduceEditorUiFrame() &&
			input.IsPressed(HIKARI::INPUT::ActionIds::ToggleEditorUI)) {
			HIKARI::SERVICES::SetEditorUIEnabled(!HIKARI::SERVICES::IsEditorUIEnabled());
		}

		if (!HIKARI::SERVICES::EndFrame()) {
			break;
		}

		if (app.IsWindowedPlayRunning() &&
			input.IsPressed(HIKARI::INPUT::ActionIds::StopPlay)) {
			app.RequestPlayStop();
		}
		if (input.IsPressed(HIKARI::INPUT::ActionIds::CloseProgram)) {
			break;
		}
	}

	app.Shutdown();
	HIKARI::SERVICES::FinalizeAll();
	return 0;
}
