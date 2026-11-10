#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Windows.h>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/App/HIKARI_EditorEngineApp.h"
#include "Editor/App/HIKARI_ProjectLauncherApp.h"
#include "Editor/Export/HIKARI_GameExportJob.h"
#include "Editor/Host/HIKARI_EditorUiServices.h"
#include "Editor/Projects/HIKARI_ProjectCreationService.h"
#include "Editor/ShaderPlayground/HIKARI_ShaderPlaygroundRunner.h"
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

// --create-project <フォルダ>：Launcher の「新建项目」と同じ手続きで項目を作って終わる。フォルダの名前が項目の名前に
// なる。成功は終了コード 0 で、標準出力に Project.hikari の場所。失敗は 2 で、標準エラー出力に理由。
std::optional<int> RunCreateProjectIfRequested() {
    const std::optional<std::filesystem::path> target = HIKARI::ReadRuntimeCommandLinePath(L"--create-project");
    if (!target) {
        return std::nullopt;
    }
    std::error_code ec{};
    const std::filesystem::path absolute = std::filesystem::absolute(*target, ec).lexically_normal();
    HIKARI::EDITOR::ProjectCreationResult created{};
    std::string message{};
    if (ec || !HIKARI::EDITOR::CreateProject({absolute.parent_path(), HIKARI::TEXT::PathToUtf8(absolute.filename())},
                                             created, message)) {
        std::fputs((ec ? ec.message() : message).c_str(), stderr);
        std::fputc('\n', stderr);
        return 2;
    }
    std::fputs(HIKARI::TEXT::PathToUtf8(created.descriptorPath).c_str(), stdout);
    std::fputc('\n', stdout);
    return 0;
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

// --shader-playground <spec.json>：編集器の shader.play と同じ実行。--project があればその Library、
// 無ければ設定のフォルダの Library/ShaderPlayground へ書く。
std::optional<int> RunHeadlessShaderPlaygroundIfRequested() {
    const std::optional<std::filesystem::path> specPath = HIKARI::ReadRuntimeCommandLinePath(L"--shader-playground");
    if (!specPath) {
        return std::nullopt;
    }
    std::filesystem::path outputRoot = std::filesystem::absolute(*specPath).parent_path() / "Library";
    if (const auto projectPath = HIKARI::ReadRuntimeCommandLinePath(L"--project")) {
        HIKARI::ProjectLaunchOptions launchOptions{};
        launchOptions.explicitProjectPath = projectPath;
        std::string message{};
        if (!HIKARI::OpenProjectForLaunch(launchOptions, &message)) {
            HIKARI_LOG_ERROR("[ShaderPlayground] " + message);
            HIKARI::GetProjectContext().Close();
            return 3;
        }
        outputRoot = HIKARI::GetProjectContext().GetLibraryRoot();
    }
    HIKARI::CORE::InitializeLogger(outputRoot / "Logs");
    nlohmann::json summary{};
    std::string error{};
    const bool ok =
        HIKARI::EDITOR::PLAYGROUND::RunShaderPlayground(*specPath, outputRoot / "ShaderPlayground", summary, error);
    if (ok) {
        HIKARI_LOG_INFO("[ShaderPlayground] wrote " + summary.value("output", std::string{}));
    } else {
        HIKARI_LOG_ERROR("[ShaderPlayground] " + error);
    }
    HIKARI::GetProjectContext().Close();
    return ok ? 0 : 5;
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
    if (const std::optional<int> created = RunCreateProjectIfRequested()) {
        return *created;
    }
    if (HIKARI::ReadRuntimeCommandLinePath(L"--import-assets") ||
        HIKARI::ReadRuntimeCommandLinePath(L"--package-game") ||
        HIKARI::ReadRuntimeCommandLinePath(L"--export-request") ||
        HIKARI::ReadRuntimeCommandLinePath(L"--shader-playground")) {
        std::string message;
        if (!HIKARI::CONTENT::OpenEngineContentForLaunch(&message)) {
            HIKARI_LOG_ERROR(message);
            return 3;
        }
        std::optional<int> exitCode = RunHeadlessShaderPlaygroundIfRequested();
        if (!exitCode)
            exitCode = RunHeadlessAssetImportIfRequested();
        if (!exitCode)
            exitCode = RunHeadlessGamePackageIfRequested();
        HIKARI::CONTENT::GetEngineContentContext().Close();
        return exitCode.value_or(2);
    }

    if (!HIKARI::EDITOR::InstallEditorUiServices()) {
        return -1;
    }
    const int exitCode = HIKARI::HasRuntimeCommandLineFlag(L"--project-hub") ? RunProjectHub() : RunEditor();
    HIKARI::EDITOR::UninstallEditorUiServices();
    return exitCode;
}
