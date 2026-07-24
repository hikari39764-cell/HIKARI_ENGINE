#include "Editor/Controllers/DocumentScene/HIKARI_DocumentToolbarController.h"

#include <cstdio>

#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    void DocumentToolbarController::SyncDocumentMeta(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync) const {
        const SceneDocument& document = scene.GetSceneDocument();
        if (context.sceneNameEditBuffer != document.sceneName) {
            context.sceneNameEditBuffer = document.sceneName;
            if (!context.saveAsNameOverriddenByUser) {
                context.saveAsNameBuffer = document.sceneName;
            }
        }
        if (context.saveAsNameBuffer.empty()) {
            context.saveAsNameBuffer = document.sceneName;
            context.saveAsNameOverriddenByUser = false;
        }
        selectionSync.SyncNextSceneObjectId(scene, context.nextSceneObjectId);
    }

    void DocumentToolbarController::Draw(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync) const {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Scene Document")) {
            ImGui::End();
            return;
        }

        DrawContents(scene, context, selectionSync);

        ImGui::End();
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
#endif
    }

    void DocumentToolbarController::DrawContents(DocumentSceneBase& scene, EditorContext& context, const SelectionSyncService& selectionSync) const {
#if defined(HIKARI_WITH_EDITOR)
        // Scene asset file actions live in Resource Workspace.
        ImGui::TextUnformatted("Scene file operations are handled in Resource Workspace.");
        ImGui::Separator();

        char sceneNameBuffer[128]{};
        std::snprintf(sceneNameBuffer, sizeof(sceneNameBuffer), "%s", context.sceneNameEditBuffer.c_str());
        if (ImGui::InputText("Scene Name", sceneNameBuffer, sizeof(sceneNameBuffer))) {
            context.sceneNameEditBuffer = sceneNameBuffer;
            if (scene.GetSceneDocument().sceneName != context.sceneNameEditBuffer) {
                scene.GetSceneDocument().sceneName = context.sceneNameEditBuffer;
                if (!context.saveAsNameOverriddenByUser) {
                    context.saveAsNameBuffer = context.sceneNameEditBuffer;
                }
                context.sceneDirty = true;
            }
        }

        if (ImGui::Button("Reload Assets")) {
            scene.ReloadAssets();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload Current Scene")) {
            scene.ReloadSceneDocument();
            context.sceneDirty = false;
            selectionSync.RebuildRuntimeWorldWithSelectionSync(scene, context.selection, context.nextSceneObjectId);
            context.sceneNameEditBuffer = scene.GetSceneDocument().sceneName;
            context.saveAsNameBuffer = scene.GetSceneDocument().sceneName;
            context.saveAsNameOverriddenByUser = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Current Scene")) {
            scene.GetSceneDocument().environment = scene.GetSceneEnvironment();
            if (scene.SaveCurrentSceneDocument()) {
                context.sceneDirty = false;
            }
        }

        ImGui::Text("Scene: %s%s", scene.GetSceneDocument().sceneName.c_str(), context.sceneDirty ? "*" : "");
        ImGui::Text("Scene Asset GUID: %s", scene.GetCurrentSceneAssetGuid().IsValid() ? scene.GetCurrentSceneAssetGuid().value.c_str() : "<transient>");
        ImGui::Text("Objects: %zu", scene.GetSceneDocument().objects.size());
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
#endif
    }

} // namespace HIKARI
