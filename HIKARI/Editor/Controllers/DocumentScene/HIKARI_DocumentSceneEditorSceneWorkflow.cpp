#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

void DocumentSceneEditorController::RequestSceneOpen(DocumentSceneBase& scene, const AssetGuid& sceneGuid) {
    if (context_.sceneDirty || scene.HasUnsavedSceneChanges()) {
        pendingSceneOpenGuid_ = sceneGuid;
        return;
    }
    pendingSceneOpenGuid_ = {};
    OpenSceneAssetFromEditor(scene, sceneGuid);
}

void DocumentSceneEditorController::DrawPendingSceneOpenModal(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
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
#else
    (void)scene;
#endif
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
