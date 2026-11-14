#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Core/HIKARI_Logger.h"
#include "Editor/Assets/Browser/HIKARI_AssetBrowserPanel.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Commands/HIKARI_EditorOperationValue.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include <filesystem>
#include <utility>

#include "imgui.h"

namespace HIKARI {
namespace {
using EDITOR::EditorOperationParameterType;

// 場面を切り替える時、今の場面に未保存の変更があったらどうするか（scene.open・scene.new の unsavedChanges）。
enum class UnsavedSceneChoice : uint8_t { Refuse, Save, Discard };
inline constexpr TEXT::EnumName<UnsavedSceneChoice> kUnsavedSceneChoiceNames[] = {
    {UnsavedSceneChoice::Refuse, "refuse"},
    {UnsavedSceneChoice::Save, "save"},
    {UnsavedSceneChoice::Discard, "discard"},
};

EDITOR::EditorOperationParameter UnsavedChangesParameter() {
    return {"unsavedChanges",
            EditorOperationParameterType::String,
            false,
            "When the current scene has unsaved changes: refuse (default; nothing happens and UNSAVED_CHANGES is "
            "returned), save (save the scene and edited assets first, like Save in the editor's unsaved-changes "
            "dialog) or discard (drop the scene's changes)",
            {{"enum", TEXT::EnumNameList(kUnsavedSceneChoiceNames)}}};
}

// name はファイル名から .scene.json を除いたもの（資産の displayName は「X.scene」になる）。
nlohmann::json DescribeSceneAsset(const AssetRecord& record) {
    return {{"guid", record.guid.value},
            {"path", record.sourcePath.generic_string()},
            {"name", EDITOR::ASSET_BROWSER::GetSceneAssetBaseName(record.sourcePath)}};
}
} // namespace

void DocumentSceneEditorController::RegisterSceneFileOperations() {
    // Play 中（開始・停止の途中を含む）は場面を作り替えない。Play は止めない。
    const auto playBusy = [this](const DocumentSceneBase& scene) {
        return scene.IsRuntimePlayActive() ||
               (aiPlaySession_ != nullptr && (aiPlaySession_->IsRunning() || aiPlaySession_->IsTransitioning() ||
                                              aiPlaySession_->HasPendingStart()));
    };
    const auto playActiveFailure = [] {
        return EDITOR::OperationFailure("PLAY_ACTIVE", "Stop Play before opening or creating a scene (Play keeps "
                                                       "running).");
    };

    EDITOR::EditorOperationDefinition open{
        "scene.open",
        "Open an existing scene asset as the editor's current scene, through the same path as opening it in the "
        "editor, without a confirmation dialog. unsavedChanges decides what happens to unsaved changes of the "
        "current scene (default refuse). Opening the scene that is already open succeeds without reloading. "
        "Refused during Play.",
        "scene",
        {{"scene", EditorOperationParameterType::String, true, "Scene asset: GUID or project path"},
         UnsavedChangesParameter()},
        [this, playBusy, playActiveFailure](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
            if (aiOperationScene_ == nullptr)
                return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
            DocumentSceneBase& scene = *aiOperationScene_;
            if (playBusy(scene))
                return playActiveFailure();
            const std::string requested = parameters.value("scene", std::string{});
            const AssetRecord* record =
                EDITOR::FindOperationAsset(scene.GetAssetDatabase(), requested, AssetType::Scene);
            if (record == nullptr)
                return EDITOR::OperationFailure("ASSET_NOT_FOUND", "Scene asset was not found: " + requested);
            nlohmann::json data = DescribeSceneAsset(*record);
            data["previousGuid"] = scene.GetCurrentSceneAssetGuid().value;
            if (record->guid == scene.GetCurrentSceneAssetGuid()) {
                const bool dirty = context_.sceneDirty || scene.HasUnsavedSceneChanges();
                data["alreadyOpen"] = true;
                data["unsavedChanges"] = dirty ? "kept" : "none";
                return EDITOR::OperationOk("The scene is already open; it was not reloaded.", std::move(data));
            }
            const AssetGuid guid = record->guid;
            std::string outcome;
            if (auto refused =
                    PrepareAgentSceneSwitch(scene, parameters.value("unsavedChanges", std::string{}), outcome))
                return std::move(*refused);
            if (!OpenSceneAssetFromEditor(scene, guid)) {
                const std::string reason = "The scene could not be opened; the current scene is unchanged. ";
                return EDITOR::OperationFailure("OPEN_FAILED", reason + scene.GetSceneLoadError());
            }
            data["alreadyOpen"] = false;
            data["unsavedChanges"] = outcome;
            return EDITOR::OperationOk("Opened scene " + scene.GetSceneDocument().sceneName + ".", std::move(data));
        }};
    open.permission = EDITOR::EditorOperationPermission::ModifyProjectFiles;
    open.searchTerms = {"open scene", "switch scene", "load scene", "打开场景", "切换场景"};
    operationRegistry_.Register(std::move(open), EDITOR::EditorOperationKind::Session, false);

    EDITOR::EditorOperationDefinition create{
        "scene.new",
        "Create a new scene asset with the default content of a new project's scene (a grid floor and the "
        "project's default systems), like New Scene Asset in the asset browser. A name goes to Assets/Scenes; "
        "path picks a folder under Assets/Scenes. An existing file name gets a number appended. Does not open it "
        "unless open is true (then unsavedChanges applies as in scene.open). Refused during Play.",
        "scene",
        {{"name", EditorOperationParameterType::String, false,
          "Scene name and file name; letters, digits, spaces, _ and - are kept (default New Scene)"},
         {"path", EditorOperationParameterType::String, false,
          "Project path of the new file under Assets/Scenes, e.g. Assets/Scenes/Levels/Boss.scene.json; instead of "
          "name"},
         {"open", EditorOperationParameterType::Bool, false,
          "Open the new scene as the current scene (default false: the user's scene stays open)"},
         UnsavedChangesParameter()},
        [this, playBusy, playActiveFailure](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
            if (aiOperationScene_ == nullptr)
                return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
            DocumentSceneBase& scene = *aiOperationScene_;
            if (playBusy(scene))
                return playActiveFailure();
            std::string baseName = parameters.value("name", std::string{});
            const std::string requestedPath = parameters.value("path", std::string{});
            std::filesystem::path directory = "Assets/Scenes";
            if (!requestedPath.empty()) {
                if (!baseName.empty())
                    return EDITOR::OperationFailure("INVALID_PARAMETER", "Give name or path, not both.");
                const std::filesystem::path path = TEXT::PathFromUtf8(requestedPath).lexically_normal();
                if (path.has_root_path() || !EDITOR::ASSET_BROWSER::IsScenesDirectoryPath(path.parent_path()))
                    return EDITOR::OperationFailure("INVALID_PATH", "path must be a project path under Assets/Scenes, "
                                                                    "e.g. Assets/Scenes/Levels/Boss.scene.json.");
                directory = path.parent_path();
                baseName = EDITOR::ASSET_BROWSER::GetSceneAssetBaseName(path);
            }
            const bool openAfter = parameters.value("open", false);
            std::string outcome = "none";
            // 開く時は、場面を作る前に未保存の変更を片付ける（断る・保存に失敗する時は何も作らない）。
            if (openAfter) {
                if (auto refused =
                        PrepareAgentSceneSwitch(scene, parameters.value("unsavedChanges", std::string{}), outcome))
                    return std::move(*refused);
            }
            AssetDatabase& database = scene.GetAssetDatabase();
            std::filesystem::path createdPath;
            std::string error;
            const AssetGuid guid = EDITOR::ASSET_BROWSER::CreateSceneAsset(
                database, directory, baseName, scene.CreateProjectDefaultSceneSystems(), createdPath, error);
            const AssetRecord* record = guid.IsValid() ? database.FindByGuid(guid) : nullptr;
            if (record == nullptr)
                return EDITOR::OperationFailure("CREATE_FAILED", "The scene could not be created. " + error);
            nlohmann::json data = DescribeSceneAsset(*record);
            HIKARI_LOG_INFO("[SceneAsset] new scene: " + createdPath.generic_string());
            if (!openAfter) {
                data["opened"] = false;
                return EDITOR::OperationOk("Created scene " + createdPath.generic_string() + ".", std::move(data));
            }
            if (!OpenSceneAssetFromEditor(scene, guid)) {
                EDITOR::EditorOperationResult result = EDITOR::OperationFailure(
                    "OPEN_FAILED", "Created scene " + createdPath.generic_string() +
                                       " but could not open it; the current scene is unchanged. " +
                                       scene.GetSceneLoadError());
                data["opened"] = false;
                result.data = std::move(data);
                return result;
            }
            data["opened"] = true;
            data["unsavedChanges"] = outcome;
            return EDITOR::OperationOk("Created and opened scene " + createdPath.generic_string() + ".",
                                       std::move(data));
        }};
    create.permission = EDITOR::EditorOperationPermission::ModifyProjectFiles;
    create.searchTerms = {"new scene", "create scene", "新建场景", "创建场景"};
    operationRegistry_.Register(std::move(create), EDITOR::EditorOperationKind::Mutation, false);
}

std::optional<EDITOR::EditorOperationResult> DocumentSceneEditorController::PrepareAgentSceneSwitch(
    DocumentSceneBase& scene, std::string_view unsavedChanges, std::string& outOutcome) {
    outOutcome = "none";
    if (!(context_.sceneDirty || scene.HasUnsavedSceneChanges()))
        return std::nullopt;
    const UnsavedSceneChoice choice =
        TEXT::ParseEnum(kUnsavedSceneChoiceNames, unsavedChanges, UnsavedSceneChoice::Refuse);
    if (choice == UnsavedSceneChoice::Discard) {
        // 変更は開いた時に捨てる（開けなかった時は残る）。資産の下書きは確認の窓の「破棄」と同じく残す。
        outOutcome = "discarded";
        return std::nullopt;
    }
    if (choice == UnsavedSceneChoice::Save) {
        // 確認の窓の「保存」と同じく、場面と未保存の資産をまとめて保存する。
        std::vector<std::string> saved;
        std::string failures;
        if (!SaveAllDocuments(scene, saved, failures))
            return EDITOR::OperationFailure("SAVE_FAILED", "Saving failed; the current scene stays open. " + failures);
        outOutcome = "saved";
        return std::nullopt;
    }
    EDITOR::EditorOperationResult result =
        EDITOR::OperationFailure("UNSAVED_CHANGES", "The current scene " + scene.GetSceneDocument().sceneName +
                                                        " has unsaved changes; nothing was changed.");
    result.hints.push_back("Pass unsavedChanges \"save\" to save them first or \"discard\" to drop them.");
    return result;
}

void DocumentSceneEditorController::RequestSceneOpen(DocumentSceneBase& scene, const AssetGuid& sceneGuid) {
    if (context_.sceneDirty || scene.HasUnsavedSceneChanges()) {
        pendingSceneOpenGuid_ = sceneGuid;
        return;
    }
    pendingSceneOpenGuid_ = {};
    OpenSceneAssetFromEditor(scene, sceneGuid);
}

void DocumentSceneEditorController::DrawPendingSceneOpenModal(DocumentSceneBase& scene) {
    if (!pendingSceneOpenGuid_.IsValid()) {
        return;
    }
    // 確認の窓は要求した所（資産一覧、視口への投下）ではなくここで開く。popup の ID は開いた窓の ID に
    // 結び付くので、別の窓の中で開くとここの BeginPopupModal と一致せず、窓が出ないまま要求が残る。
    constexpr const char* kPopupId = "###UnsavedSceneChanges";
    if (!ImGui::IsPopupOpen(kPopupId)) {
        ImGui::OpenPopup(kPopupId);
    }
    bool openModal = true;
    if (!ImGui::BeginPopupModal((TEXT::Localize("ui.sceneOpen.unsavedTitle") + kPopupId).c_str(), &openModal,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!openModal) {
            // 閉じるボタンは取り消しと同じ。
            pendingSceneOpenGuid_ = {};
            viewportDropMessage_ = TEXT::Localize("ui.sceneOpen.cancelled");
        }
        return;
    }

    ImGui::TextUnformatted(TEXT::Localize("ui.sceneOpen.unsavedBody").c_str());
    ImGui::TextUnformatted((TEXT::Localize("shell.unsavedList") + UnsavedDocumentSummary(scene)).c_str());
    ImGui::TextDisabled("%s", TEXT::Localize("ui.sceneOpen.unsavedHint").c_str());
    ImGui::Separator();

    // 保存は場面と未保存の資産をまとめて保存する（Ctrl+S と同じ）。破棄は場面の変更だけ（資産の下書きは残る）。
    if (ImGui::Button(TEXT::Localize("menu.edit.saveAll").c_str(), ImVec2(96.0f, 0.0f))) {
        std::vector<std::string> saved;
        std::string failures;
        if (SaveAllDocuments(scene, saved, failures)) {
            OpenSceneAssetFromEditor(scene, pendingSceneOpenGuid_);
            pendingSceneOpenGuid_ = {};
            ImGui::CloseCurrentPopup();
        } else {
            viewportDropMessage_ = TEXT::Localize("ui.sceneOpen.saveFailed") + " " + failures;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("ui.sceneOpen.discard").c_str(), ImVec2(96.0f, 0.0f))) {
        context_.sceneDirty = false;
        scene.SetUnsavedSceneChanges(false);
        OpenSceneAssetFromEditor(scene, pendingSceneOpenGuid_);
        pendingSceneOpenGuid_ = {};
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("ui.common.cancel").c_str(), ImVec2(96.0f, 0.0f))) {
        pendingSceneOpenGuid_ = {};
        viewportDropMessage_ = TEXT::Localize("ui.sceneOpen.cancelled");
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

bool DocumentSceneEditorController::OpenSceneAssetFromEditor(DocumentSceneBase& scene, const AssetGuid& sceneGuid) {
    if (!sceneGuid.IsValid()) {
        viewportDropMessage_ = TEXT::Localize("ui.sceneOpen.invalidAsset");
        return false;
    }

    const SceneObjectId activeObjectId = context_.selection.GetActiveObjectId();
    const bool opened = scene.OpenSceneAssetNow(sceneGuid);
    if (!opened) {
        selectionSync_.SynchronizeAfterRuntimeWorldRebuild(scene, context_.selection, context_.nextSceneObjectId,
                                                           activeObjectId);
        viewportDropMessage_ = TEXT::Localize("ui.sceneOpen.openFailed");
        HIKARI_LOG_WARN("[SceneAsset] open scene failed: " + sceneGuid.value);
        return false;
    }

    context_.selection.ClearObjects();
    context_.selection.selectedAsset = nullptr;
    context_.selection.selectedAssetGuid = sceneGuid.value;
    context_.selection.selectedAssetPath.clear();
    context_.sceneDirty = false;
    scene.SetUnsavedSceneChanges(false);
    context_.sceneNameEditBuffer = scene.GetSceneDocument().sceneName;
    context_.saveAsNameBuffer = scene.GetSceneDocument().sceneName;
    selectionSync_.SyncNextSceneObjectId(scene, context_.nextSceneObjectId);
    viewportDropMessage_ = TEXT::Localize("ui.sceneOpen.opened") + " " + scene.GetSceneDocument().sceneName;
    HIKARI_LOG_INFO("[SceneAsset] open scene: " + sceneGuid.value);
    return true;
}

} // namespace HIKARI
