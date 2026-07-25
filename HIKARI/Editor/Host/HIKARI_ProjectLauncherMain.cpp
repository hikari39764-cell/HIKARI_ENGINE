#include <Windows.h>

#include "Editor/App/HIKARI_ProjectLauncherApp.h"
#include "Editor/Host/HIKARI_EditorUiServices.h"
#include "Runtime/Host/HIKARI_EngineHostRunner.h"

#if !defined(HIKARI_ENGINE_SOURCE_ROOT)
#error HIKARI_ENGINE_SOURCE_ROOT must identify the engine checkout
#endif

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (!HIKARI::EDITOR::InstallEditorUiServices()) {
        return -1;
    }

    HIKARI::EDITOR::ProjectLauncherApp application{
        HIKARI_ENGINE_SOURCE_ROOT
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
