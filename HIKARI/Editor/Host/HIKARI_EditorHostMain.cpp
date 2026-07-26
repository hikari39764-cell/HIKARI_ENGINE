#include <Windows.h>

#include "Editor/App/HIKARI_EditorEngineApp.h"
#include "Editor/Host/HIKARI_EditorUiServices.h"
#include "Runtime/Host/HIKARI_EngineHostRunner.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (!HIKARI::EDITOR::InstallEditorUiServices()) {
        return -1;
    }

    HIKARI::EDITOR::EditorEngineApp application{};
    HIKARI::RUNTIME::EngineHostRunOptions options{};
    options.product =
        HIKARI::RUNTIME::EngineHostProduct::Editor;
    const int exitCode =
        HIKARI::RUNTIME::RunEngineHost(application, options);

    HIKARI::EDITOR::UninstallEditorUiServices();
    return exitCode;
}
