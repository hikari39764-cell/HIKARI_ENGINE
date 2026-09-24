#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <Windows.h>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Editor/App/HIKARI_EditorEngineApp.h"
#include "Editor/Export/HIKARI_GameExporter.h"
#include "Editor/Export/HIKARI_GameExportJob.h"
#include "Editor/Host/HIKARI_EditorUiServices.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Project/HIKARI_ProjectLaunch.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "Runtime/Host/HIKARI_EngineHostRunner.h"

namespace {

bool IsSafeAuthoredAssetScope(const std::filesystem::path& scope) {
    if (scope.empty() || scope.is_absolute()) {
        return false;
    }
    const std::string normalized = scope.lexically_normal().generic_string();
    return normalized == "Assets" || normalized.rfind("Assets/", 0u) == 0u;
}

std::optional<int> RunHeadlessAssetImportIfRequested() {
    const std::optional<std::filesystem::path> assetScope = HIKARI::ReadRuntimeCommandLinePath(L"--import-assets");
    if (!assetScope) {
        return std::nullopt;
    }
    if (!IsSafeAuthoredAssetScope(*assetScope)) {
        OutputDebugStringA("[AssetImporter] --import-assets must be a project-relative "
                           "Assets directory.\n");
        return 2;
    }

    const std::optional<std::filesystem::path> projectPath = HIKARI::ReadRuntimeCommandLinePath(L"--project");
    if (!projectPath) {
        OutputDebugStringA("[AssetImporter] --project is required with "
                           "--import-assets.\n");
        return 2;
    }

    HIKARI::ProjectLaunchOptions launchOptions{};
    launchOptions.explicitProjectPath = projectPath;
    std::string message{};
    if (!HIKARI::OpenProjectForLaunch(launchOptions, &message) || !HIKARI::ApplyProjectWorkingDirectory(&message)) {
        OutputDebugStringA(("[AssetImporter] " + message + "\n").c_str());
        HIKARI::GetProjectContext().Close();
        return 3;
    }

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) {
        OutputDebugStringA("[AssetImporter] Could not initialize COM for texture "
                           "import.\n");
        HIKARI::GetProjectContext().Close();
        return 4;
    }

    HIKARI::AssetDatabase assetDatabase{};
    if (!assetDatabase.Initialize(HIKARI::GetProjectContext().GetRoot()) || !assetDatabase.ScanAssets(false)) {
        OutputDebugStringA("[AssetImporter] Could not scan authored assets.\n");
        CoUninitialize();
        HIKARI::GetProjectContext().Close();
        return 4;
    }
    const std::vector<const HIKARI::AssetRecord*> records =
        assetDatabase.CollectInDirectory(assetScope->lexically_normal(), true);
    std::vector<HIKARI::AssetGuid> assetGuids{};
    assetGuids.reserve(records.size());
    for (const HIKARI::AssetRecord* record : records) {
        if (record != nullptr) {
            if (!record->guid.IsValid() && !record->metaExists && !assetDatabase.RegenerateMeta(record->sourcePath)) {
                CoUninitialize();
                HIKARI::GetProjectContext().Close();
                return 5;
            }
            assetGuids.push_back(record->guid);
        }
    }
    const HIKARI::AssetImportBatchResult result = assetDatabase.ImportAssets(assetGuids);
    const std::string resultMessage = "[AssetImporter] scope=" + assetScope->lexically_normal().generic_string() +
                                      ", attempted=" + std::to_string(result.attempted) +
                                      ", succeeded=" + std::to_string(result.succeeded) +
                                      ", failed=" + std::to_string(result.failed) + "\n";
    OutputDebugStringA(resultMessage.c_str());
    CoUninitialize();
    HIKARI::GetProjectContext().Close();
    return result.failed == 0 ? 0 : 5;
}

std::optional<int> RunHeadlessGamePackageIfRequested() {
    const auto requestPath = HIKARI::ReadRuntimeCommandLinePath(L"--export-request");
    const std::optional<std::filesystem::path> outputDirectory = HIKARI::ReadRuntimeCommandLinePath(L"--package-game");
    if (!outputDirectory && !requestPath) {
        return std::nullopt;
    }

    const std::optional<std::filesystem::path> projectPath = HIKARI::ReadRuntimeCommandLinePath(L"--project");
    if (!projectPath) {
        OutputDebugStringA("[GamePackager] --project is required with "
                           "--package-game.\n");
        return 2;
    }

    HIKARI::ProjectLaunchOptions launchOptions{};
    launchOptions.explicitProjectPath = projectPath;
    std::string message{};
    if (!HIKARI::OpenProjectForLaunch(launchOptions, &message) || !HIKARI::ApplyProjectWorkingDirectory(&message)) {
        OutputDebugStringA(("[GamePackager] " + message + "\n").c_str());
        HIKARI::GetProjectContext().Close();
        return 3;
    }

    HIKARI::EDITOR::GameExportOptions exportOptions{};
    if (requestPath) {
        const int code = HIKARI::EDITOR::GameExportJob::RunRequest(*requestPath);
        HIKARI::GetProjectContext().Close();
        return code;
    }
    exportOptions.outputDirectory = *outputDirectory;

    HIKARI::ProjectSettingsService settings{};
    if (settings.Load(HIKARI::GetProjectContext().GetRoot())) {
        const HIKARI::AssetGuid startupSceneGuid = settings.GetSettings().startupSceneGuid;
        if (HIKARI::IsValidAssetGuid(startupSceneGuid.value)) {
            exportOptions.startupSceneGuid = startupSceneGuid.value;
            exportOptions.sceneGuids.push_back(startupSceneGuid.value);
        }
    }

    const HIKARI::EDITOR::GameExportResult result = HIKARI::EDITOR::GameExporter::Export(exportOptions);
    OutputDebugStringA((std::string("[GamePackager] ") + result.message + "\n").c_str());
    HIKARI::GetProjectContext().Close();
    return result.success ? 0 : 4;
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (HIKARI::ReadRuntimeCommandLinePath(L"--import-assets") ||
        HIKARI::ReadRuntimeCommandLinePath(L"--package-game") ||
        HIKARI::ReadRuntimeCommandLinePath(L"--export-request")) {
        std::string message;
        if (!HIKARI::CONTENT::OpenEngineContentForLaunch(&message)) {
            OutputDebugStringA(message.c_str());
            return 3;
        }
        const auto importExitCode = RunHeadlessAssetImportIfRequested();
        const int exitCode = importExitCode ? *importExitCode : RunHeadlessGamePackageIfRequested().value_or(2);
        HIKARI::CONTENT::GetEngineContentContext().Close();
        return exitCode;
    }

    if (!HIKARI::EDITOR::InstallEditorUiServices()) {
        return -1;
    }

    HIKARI::EDITOR::EditorEngineApp application{};
    HIKARI::RUNTIME::EngineHostRunOptions options{};
    const auto readyName = HIKARI::ReadRuntimeCommandLinePath(L"--launcher-ready-event");
    if (readyName && readyName->wstring().starts_with(L"Local\\HIKARI.EditorReady.")) {
        options.onFirstApplicationPresented = [name = readyName->wstring()] {
            if (HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str())) {
                SetEvent(event);
                CloseHandle(event);
            }
        };
    }
    options.product = HIKARI::RUNTIME::EngineHostProduct::Editor;
    const int exitCode = HIKARI::RUNTIME::RunEngineHost(application, options);

    HIKARI::EDITOR::UninstallEditorUiServices();
    return exitCode;
}
