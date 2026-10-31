#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Windows.h>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/HIKARI_Logger.h"
#include "Editor/App/HIKARI_EditorEngineApp.h"
#include "Editor/App/HIKARI_ProjectLauncherApp.h"
#include "Editor/Export/HIKARI_GameExportJob.h"
#include "Editor/Host/HIKARI_EditorUiServices.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Project/HIKARI_ProjectLaunch.h"
#include "Core/Paths/HIKARI_ExecutablePath.h"
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
        HIKARI_LOG_ERROR("[AssetImporter] --import-assets must be a project-relative Assets directory.");
        return 2;
    }

    const std::optional<std::filesystem::path> projectPath = HIKARI::ReadRuntimeCommandLinePath(L"--project");
    if (!projectPath) {
        HIKARI_LOG_ERROR("[AssetImporter] --project is required with --import-assets.");
        return 2;
    }

    HIKARI::ProjectLaunchOptions launchOptions{};
    launchOptions.explicitProjectPath = projectPath;
    std::string message{};
    if (!HIKARI::OpenProjectForLaunch(launchOptions, &message) || !HIKARI::ApplyProjectWorkingDirectory(&message)) {
        HIKARI_LOG_ERROR("[AssetImporter] " + message);
        HIKARI::GetProjectContext().Close();
        return 3;
    }
    HIKARI::CORE::InitializeLogger(HIKARI::GetProjectContext().GetLibraryRoot() / "Logs");

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(comResult)) {
        HIKARI_LOG_ERROR("[AssetImporter] Could not initialize COM for texture import.");
        HIKARI::GetProjectContext().Close();
        return 4;
    }

    HIKARI::AssetDatabase assetDatabase{};
    if (!assetDatabase.Initialize(HIKARI::GetProjectContext().GetRoot()) || !assetDatabase.ScanAssets(false)) {
        HIKARI_LOG_ERROR("[AssetImporter] Could not scan authored assets.");
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
                                      ", failed=" + std::to_string(result.failed);
    HIKARI::CORE::Log(result.failed == 0 ? HIKARI::CORE::LogLevel::Info : HIKARI::CORE::LogLevel::Error, resultMessage);
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
        HIKARI_LOG_ERROR("[GamePackager] --project is required with --package-game.");
        return 2;
    }

    HIKARI::ProjectLaunchOptions launchOptions{};
    launchOptions.explicitProjectPath = projectPath;
    std::string message{};
    if (!HIKARI::OpenProjectForLaunch(launchOptions, &message) || !HIKARI::ApplyProjectWorkingDirectory(&message)) {
        HIKARI_LOG_ERROR("[GamePackager] " + message);
        HIKARI::GetProjectContext().Close();
        return 3;
    }
    HIKARI::CORE::InitializeLogger(HIKARI::GetProjectContext().GetLibraryRoot() / "Logs");

    // --package-game は編集器の書き出しと同じ要求を作り、同じ経路（Shipping 構築・識別情報の検証）で実行する。
    std::filesystem::path request = requestPath.value_or(std::filesystem::path{});
    if (!requestPath) {
        HIKARI::EDITOR::GameExportOptions exportOptions{};
        exportOptions.outputDirectory = *outputDirectory;
        HIKARI::ProjectSettingsService settings{};
        if (settings.Load(HIKARI::GetProjectContext().GetRoot())) {
            const HIKARI::AssetGuid startupSceneGuid = settings.GetSettings().startupSceneGuid;
            if (HIKARI::IsValidAssetGuid(startupSceneGuid.value)) {
                exportOptions.startupSceneGuid = startupSceneGuid.value;
                exportOptions.sceneGuids.push_back(startupSceneGuid.value);
            }
        }
        request = HIKARI::EDITOR::GameExportJob::WriteRequest(exportOptions, message);
        if (request.empty()) {
            HIKARI_LOG_ERROR("[GamePackager] " + message);
            HIKARI::GetProjectContext().Close();
            return 4;
        }
    }
    const int code = HIKARI::EDITOR::GameExportJob::RunRequest(request);
    HIKARI::GetProjectContext().Close();
    return code;
}

// 配布ルートの Launcher から --project-hub で起動される項目中心。
int RunProjectHub() {
    HIKARI::EDITOR::ProjectLauncherStartupRequest startup{};
    startup.rebuildProject = HIKARI::ReadRuntimeCommandLinePath(L"--rebuild-project");
    startup.waitForProcessId = HIKARI::ReadRuntimeCommandLineUint32(L"--wait-for-process-id");
    for (const wchar_t* flag : {L"--agent-develop-project", L"--agent-auto-approve"}) {
        if (startup.rebuildProject && HIKARI::HasRuntimeCommandLineFlag(flag))
            startup.editorFlags.emplace_back(flag);
    }
    startup.waitForProcessIdMalformed = startup.rebuildProject.has_value() &&
                                        (!startup.waitForProcessId.has_value() || *startup.waitForProcessId == 0u);

    HIKARI::EDITOR::ProjectLauncherApp application{HIKARI::PLATFORM::GetExecutableDirectory(), std::move(startup)};
    HIKARI::RUNTIME::EngineHostRunOptions options{};
    options.product = HIKARI::RUNTIME::EngineHostProduct::Editor;
    options.projectPolicy = HIKARI::RUNTIME::EngineHostProjectPolicy::None;
    options.loadRuntimeConfig = false;
    options.windowTitle = "HIKARI Project Hub";
    options.enableStreamline = false;
    return HIKARI::RUNTIME::RunEngineHost(application, options);
}

int RunEditor() {
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
    return HIKARI::RUNTIME::RunEngineHost(application, options);
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (HIKARI::ReadRuntimeCommandLinePath(L"--import-assets") ||
        HIKARI::ReadRuntimeCommandLinePath(L"--package-game") ||
        HIKARI::ReadRuntimeCommandLinePath(L"--export-request")) {
        std::string message;
        if (!HIKARI::CONTENT::OpenEngineContentForLaunch(&message)) {
            HIKARI_LOG_ERROR(message);
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
    const int exitCode = HIKARI::HasRuntimeCommandLineFlag(L"--project-hub") ? RunProjectHub() : RunEditor();
    HIKARI::EDITOR::UninstallEditorUiServices();
    return exitCode;
}
