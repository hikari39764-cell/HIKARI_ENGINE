#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Core/HIKARI_Logger.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    void DocumentSceneEditorController::DrawPendingSceneOpenModal(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        bool openModal = true;
        if (!ImGui::BeginPopupModal(
                (TEXT::Localize("ui.sceneOpen.unsavedTitle") +
                    "###UnsavedSceneChanges").c_str(),
                &openModal,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        ImGui::TextUnformatted(
            TEXT::Localize("ui.sceneOpen.unsavedBody").c_str());
        ImGui::TextDisabled(
            "%s",
            TEXT::Localize("ui.sceneOpen.unsavedHint").c_str());
        ImGui::Separator();

        if (ImGui::Button(
                TEXT::Localize("ui.common.save").c_str(),
                ImVec2(96.0f, 0.0f))) {
            if (scene.SaveCurrentSceneDocument()) {
                context_.sceneDirty = false;
                OpenSceneAssetFromEditor(scene, pendingSceneOpenGuid_);
                pendingSceneOpenGuid_ = {};
                ImGui::CloseCurrentPopup();
            } else {
                viewportDropMessage_ =
                    TEXT::Localize("ui.sceneOpen.saveFailed");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(
                TEXT::Localize("ui.sceneOpen.discard").c_str(),
                ImVec2(96.0f, 0.0f))) {
            context_.sceneDirty = false;
            scene.SetUnsavedSceneChanges(false);
            OpenSceneAssetFromEditor(scene, pendingSceneOpenGuid_);
            pendingSceneOpenGuid_ = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(
                TEXT::Localize("ui.common.cancel").c_str(),
                ImVec2(96.0f, 0.0f))) {
            pendingSceneOpenGuid_ = {};
            viewportDropMessage_ =
                TEXT::Localize("ui.sceneOpen.cancelled");
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
#else
        (void)scene;
#endif
    }

    bool DocumentSceneEditorController::OpenSceneAssetFromEditor(DocumentSceneBase& scene, const AssetGuid& sceneGuid) {
        if (!sceneGuid.IsValid()) {
            viewportDropMessage_ =
                TEXT::Localize("ui.sceneOpen.invalidAsset");
            return false;
        }

        const bool opened = scene.OpenSceneAssetNow(sceneGuid);
        if (!opened) {
            viewportDropMessage_ =
                TEXT::Localize("ui.sceneOpen.openFailed");
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
        viewportDropMessage_ =
            TEXT::Localize("ui.sceneOpen.opened") + " " +
            scene.GetSceneDocument().sceneName;
        HIKARI_LOG_INFO("[SceneAsset] open scene: " + sceneGuid.value);
        return true;
    }

} // namespace HIKARI
