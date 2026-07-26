#include <Windows.h>

#include "Editor/App/HIKARI_ProjectLauncherApp.h"
#include "Editor/Host/HIKARI_EditorUiServices.h"
#include "Platform/Paths/HIKARI_ExecutablePath.h"
#include "Runtime/Host/HIKARI_EngineHostRunner.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (!HIKARI::EDITOR::InstallEditorUiServices()) {
        return -1;
    }

    HIKARI::EDITOR::ProjectLauncherApp application{
        HIKARI::PLATFORM::GetExecutableDirectory()
    };
    HIKARI::RUNTIME::EngineHostRunOptions options{};
    options.product =
        HIKARI::RUNTIME::EngineHostProduct::Editor;
    options.projectPolicy =
        HIKARI::RUNTIME::EngineHostProjectPolicy::None;
    options.loadRuntimeConfig = false;
    options.windowTitle = "HIKARI Project Hub";
    const int exitCode =
        HIKARI::RUNTIME::RunEngineHost(
            application,
            options);

    HIKARI::EDITOR::UninstallEditorUiServices();
    return exitCode;
}
