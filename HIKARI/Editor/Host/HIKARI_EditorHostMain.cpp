#include <filesystem>
#include <optional>
#include <string>

#include <Windows.h>

#include "Editor/App/HIKARI_EditorEngineApp.h"
#include "Editor/Export/HIKARI_GameExporter.h"
#include "Editor/Host/HIKARI_EditorUiServices.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Project/HIKARI_ProjectLaunch.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "Runtime/Host/HIKARI_EngineHostRunner.h"

namespace {

    std::optional<int> RunHeadlessGamePackageIfRequested() {
        const std::optional<std::filesystem::path> outputDirectory =
            HIKARI::ReadRuntimeCommandLinePath(
                L"--package-game");
        if (!outputDirectory) {
            return std::nullopt;
        }

        const std::optional<std::filesystem::path> projectPath =
            HIKARI::ReadRuntimeCommandLinePath(
                L"--project");
        if (!projectPath) {
            OutputDebugStringA(
                "[GamePackager] --project is required with "
                "--package-game.\n");
            return 2;
        }

        HIKARI::ProjectLaunchOptions launchOptions{};
        launchOptions.explicitProjectPath = projectPath;
        std::string message{};
        if (!HIKARI::OpenProjectForLaunch(
                launchOptions,
                &message) ||
            !HIKARI::ApplyProjectWorkingDirectory(
                &message)) {
            OutputDebugStringA(
                ("[GamePackager] " + message + "\n").c_str());
            HIKARI::GetProjectContext().Close();
            return 3;
        }

        HIKARI::EDITOR::GameExportOptions exportOptions{};
        exportOptions.outputDirectory =
            *outputDirectory;

        HIKARI::ProjectSettingsService settings{};
        if (settings.Load(
                HIKARI::GetProjectContext().GetRoot())) {
            const HIKARI::AssetGuid startupSceneGuid =
                settings.GetSettings().startupSceneGuid;
            if (startupSceneGuid.IsValid()) {
                exportOptions.startupSceneGuid =
                    startupSceneGuid.value;
                exportOptions.sceneGuids.push_back(
                    startupSceneGuid.value);
            }
        }

        const HIKARI::EDITOR::GameExportResult result =
            HIKARI::EDITOR::GameExporter::Export(
                exportOptions);
        OutputDebugStringA(
            (
                std::string("[GamePackager] ") +
                result.message + "\n")
                .c_str());
        HIKARI::GetProjectContext().Close();
        return result.success ? 0 : 4;
    }

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (const std::optional<int> packageExitCode =
            RunHeadlessGamePackageIfRequested()) {
        return *packageExitCode;
    }

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
