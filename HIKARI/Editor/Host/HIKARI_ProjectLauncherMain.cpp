#include <Windows.h>

#include <utility>

#include "Editor/App/HIKARI_ProjectLauncherApp.h"
#include "Editor/Host/HIKARI_EditorUiServices.h"
#include "Platform/Paths/HIKARI_ExecutablePath.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "Runtime/Host/HIKARI_EngineHostRunner.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (!HIKARI::EDITOR::InstallEditorUiServices()) {
        return -1;
    }

    HIKARI::EDITOR::ProjectLauncherStartupRequest startup{};
    startup.rebuildProject = HIKARI::ReadRuntimeCommandLinePath(L"--rebuild-project");
    startup.waitForProcessId = HIKARI::ReadRuntimeCommandLineUint32(L"--wait-for-process-id");
    startup.waitForProcessIdMalformed = startup.rebuildProject.has_value() &&
                                        (!startup.waitForProcessId.has_value() || *startup.waitForProcessId == 0u);

    HIKARI::EDITOR::ProjectLauncherApp application{HIKARI::PLATFORM::GetExecutableDirectory(), std::move(startup)};
    HIKARI::RUNTIME::EngineHostRunOptions options{};
    options.product = HIKARI::RUNTIME::EngineHostProduct::Editor;
    options.projectPolicy = HIKARI::RUNTIME::EngineHostProjectPolicy::None;
    options.loadRuntimeConfig = false;
    options.windowTitle = "HIKARI Project Hub";
    options.enableStreamline = false;
    const int exitCode = HIKARI::RUNTIME::RunEngineHost(application, options);

    HIKARI::EDITOR::UninstallEditorUiServices();
    return exitCode;
}
