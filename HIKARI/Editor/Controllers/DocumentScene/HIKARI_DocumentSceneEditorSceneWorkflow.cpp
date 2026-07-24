#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Core/HIKARI_Logger.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    void DocumentSceneEditorController::DrawPendingSceneOpenModal(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        bool openModal = true;
        if (!ImGui::BeginPopupModal("Unsaved Scene Changes", &openModal, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        ImGui::TextUnformatted("Current scene has unsaved changes.");
        ImGui::TextDisabled("Save before opening the dropped Scene asset?");
        ImGui::Separator();

        if (ImGui::Button("Save", ImVec2(96.0f, 0.0f))) {
            if (scene.SaveCurrentSceneDocument()) {
                context_.sceneDirty = false;
                OpenSceneAssetFromEditor(scene, pendingSceneOpenGuid_);
                pendingSceneOpenGuid_ = {};
                ImGui::CloseCurrentPopup();
            } else {
                viewportDropMessage_ = "Save failed; scene was not opened";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard", ImVec2(96.0f, 0.0f))) {
            context_.sceneDirty = false;
            scene.SetUnsavedSceneChanges(false);
            OpenSceneAssetFromEditor(scene, pendingSceneOpenGuid_);
            pendingSceneOpenGuid_ = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f))) {
            pendingSceneOpenGuid_ = {};
            viewportDropMessage_ = "Scene open cancelled";
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
#else
        (void)scene;
#endif
    }

    bool DocumentSceneEditorController::OpenSceneAssetFromEditor(DocumentSceneBase& scene, const AssetGuid& sceneGuid) {
        if (!sceneGuid.IsValid()) {
            viewportDropMessage_ = "Invalid scene asset";
            return false;
        }

        const bool opened = scene.OpenSceneAssetNow(sceneGuid);
        if (!opened) {
            viewportDropMessage_ = "Scene asset open failed";
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
        viewportDropMessage_ = "Scene opened: " + scene.GetSceneDocument().sceneName;
        HIKARI_LOG_INFO("[SceneAsset] open scene: " + sceneGuid.value);
        return true;
    }

} // namespace HIKARI
