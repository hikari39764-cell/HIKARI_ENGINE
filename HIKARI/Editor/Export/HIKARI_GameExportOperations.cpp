#include "Editor/Export/HIKARI_GameExportJob.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectContext.h"

namespace HIKARI::EDITOR {
void RegisterGameExportOperations(EditorOperationRegistry& registry, GameExportJob& job,
                                  std::function<bool()> canStart) {
    const auto registerExportOperation = [&registry](const char* id, const char* summary,
                                                     EditorOperationDefinition operation) {
        operation.id = id;
        operation.summary = summary;
        operation.requiresDocumentSession = false;
        registry.Register(std::move(operation));
    };
    EditorOperationDefinition start;
    start.workspace = "general";
    start.kind = EditorOperationKind::Session;
    start.permission = EditorOperationPermission::ExecuteExternalProcess;
    start.parameters = {
        {"outputDirectory", EditorOperationParameterType::String, true, "出力先フォルダー"},
        {"startupSceneGuid", EditorOperationParameterType::String, false, "開始シーン GUID"},
        {"sceneGuids", EditorOperationParameterType::Json, false, "出力するシーン GUID の配列"},
        {"windowMode", EditorOperationParameterType::Int, false, "0: Windowed / 1: Borderless / 2: Fullscreen"}};
    start.execute = [&job, canStart](const nlohmann::json& parameters) -> EditorOperationResult {
        if (!canStart())
            return {false, "EXPORT_NOT_READY", "Play を停止し、変更を保存してから出力してください。"};
        const int mode = parameters.value("windowMode", 1);
        if (mode < 0 || mode > 2)
            return {false, "INVALID_WINDOW_MODE", "Unknown window mode."};
        GameExportOptions options;
        options.outputDirectory = TEXT::PathFromUtf8(parameters.at("outputDirectory").get<std::string>());
        options.startupSceneGuid = parameters.value("startupSceneGuid", std::string{});
        options.sceneGuids = parameters.value("sceneGuids", std::vector<std::string>{});
        options.windowMode = static_cast<GameExportWindowMode>(mode);
        std::string message;
        if (!job.Start(options, message))
            return {false, "EXPORT_START_FAILED", message};
        return {true, "EXPORT_STARTED", "エクスポートを開始しました。", job.Status()};
    };
    registerExportOperation("export.start", "保存済みのプロジェクトを Shipping ビルドし、検証してゲームを出力します。",
                            std::move(start));
    const auto statusOperation = [&job](bool cancel) {
        EditorOperationDefinition operation;
        operation.workspace = "general";
        operation.kind = cancel ? EditorOperationKind::Session : EditorOperationKind::Query;
        operation.permission =
            cancel ? EditorOperationPermission::ModifyEditorState : EditorOperationPermission::ReadEditorState;
        operation.execute = [&job, cancel](const nlohmann::json&) -> EditorOperationResult {
            if (cancel)
                job.Cancel();
            job.Poll();
            return {true, "OK", job.Status().value("message", ""), job.Status()};
        };
        return operation;
    };
    registerExportOperation("export.status", "出力の進捗と結果を取得します。", statusOperation(false));
    registerExportOperation("export.cancel", "出力を安全にキャンセルします。", statusOperation(true));

    EditorOperationDefinition getIdentity;
    getIdentity.workspace = "general";
    getIdentity.kind = EditorOperationKind::Query;
    getIdentity.permission = EditorOperationPermission::ReadEditorState;
    getIdentity.execute = [](const nlohmann::json&) -> EditorOperationResult {
        if (!GetProjectContext().IsOpen())
            return {false, "PROJECT_NOT_OPEN", "プロジェクトを開いてください。"};
        GameExportIdentity identity;
        std::string message;
        if (!LoadGameExportIdentity(GetProjectContext().GetRoot(), identity, message))
            return {false, "EXPORT_SETTINGS_READ_FAILED", message};
        return {true, "OK", "ゲーム情報を読み込みました。", GameExportIdentityJson(identity)};
    };
    registerExportOperation("export.settings.get", "EXE 名、ウィンドウタイトル、アイコンの出力設定を取得します。",
                            std::move(getIdentity));

    EditorOperationDefinition setIdentity;
    setIdentity.workspace = "general";
    setIdentity.kind = EditorOperationKind::Mutation;
    setIdentity.permission = EditorOperationPermission::ModifyProjectFiles;
    setIdentity.parameters = {
        {"executableName", EditorOperationParameterType::String, true, "出力 EXE ファイル名"},
        {"windowTitle", EditorOperationParameterType::String, true, "ウィンドウタイトル（空欄で既定値）"},
        {"iconPath", EditorOperationParameterType::String, true,
         "ICO のパス（空欄で既定値。絶対パスは工程内へ取り込み）"}};
    setIdentity.execute = [&job](const nlohmann::json& parameters) -> EditorOperationResult {
        if (!GetProjectContext().IsOpen())
            return {false, "PROJECT_NOT_OPEN", "プロジェクトを開いてください。"};
        if (job.IsRunning())
            return {false, "EXPORT_BUSY", "出力が完了してから変更してください。"};
        GameExportIdentity identity;
        std::string message;
        if (!ParseGameExportIdentity(parameters, identity, message) ||
            !SaveGameExportIdentity(GetProjectContext().GetRoot(), identity, message))
            return {false, "EXPORT_SETTINGS_SAVE_FAILED", message};
        if (!LoadGameExportIdentity(GetProjectContext().GetRoot(), identity, message))
            return {false, "EXPORT_SETTINGS_READ_FAILED", message};
        return {true, "EXPORT_SETTINGS_SAVED", "ゲーム情報を保存しました。", GameExportIdentityJson(identity)};
    };
    registerExportOperation("export.settings.set",
                            "ゲームの出力設定を保存し、外部の ICO をプロジェクトへ取り込みます。",
                            std::move(setIdentity));
}
} // namespace HIKARI::EDITOR
