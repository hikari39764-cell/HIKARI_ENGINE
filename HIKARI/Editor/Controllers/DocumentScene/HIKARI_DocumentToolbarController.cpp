#include "Editor/Controllers/DocumentScene/HIKARI_DocumentToolbarController.h"

#include <cstdio>

#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

void DocumentToolbarController::SyncDocumentMeta(DocumentSceneBase& scene, EditorContext& context,
                                                 const SelectionSyncService& selectionSync) const {
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

void DocumentToolbarController::Draw(DocumentSceneBase& scene, EditorContext& context,
                                     const SelectionSyncService& selectionSync) const {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin(TEXT::LocalizeWindow("ui.window.sceneDocument", "Scene Document").c_str())) {
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

void DocumentToolbarController::DrawContents(DocumentSceneBase& scene, EditorContext& context,
                                             const SelectionSyncService& selectionSync) const {
#if defined(HIKARI_WITH_EDITOR)
    // Scene asset file actions live in Resource Workspace.
    ImGui::TextUnformatted(TEXT::Localize("ui.toolbar.fileNote").c_str());
    ImGui::Separator();

    char sceneNameBuffer[128]{};
    std::snprintf(sceneNameBuffer, sizeof(sceneNameBuffer), "%s", context.sceneNameEditBuffer.c_str());
    if (ImGui::InputText(TEXT::Localize("ui.toolbar.sceneName").c_str(), sceneNameBuffer, sizeof(sceneNameBuffer))) {
        context.sceneNameEditBuffer = sceneNameBuffer;
        if (scene.GetSceneDocument().sceneName != context.sceneNameEditBuffer) {
            scene.GetSceneDocument().sceneName = context.sceneNameEditBuffer;
            if (!context.saveAsNameOverriddenByUser) {
                context.saveAsNameBuffer = context.sceneNameEditBuffer;
            }
            context.sceneDirty = true;
        }
    }

    if (ImGui::Button(TEXT::Localize("ui.toolbar.reloadAssets").c_str())) {
        if (!scene.ReloadAssets()) {
            HIKARI_LOG_WARN("[SceneAsset] toolbar asset refresh was rejected.");
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("ui.toolbar.reloadScene").c_str())) {
        const SceneObjectId activeObjectId = context.selection.GetActiveObjectId();
        const bool reloaded = scene.ReloadSceneDocument();
        selectionSync.SynchronizeAfterRuntimeWorldRebuild(scene, context.selection, context.nextSceneObjectId,
                                                          activeObjectId);
        if (reloaded) {
            context.sceneDirty = false;
            context.sceneNameEditBuffer = scene.GetSceneDocument().sceneName;
            context.saveAsNameBuffer = scene.GetSceneDocument().sceneName;
            context.saveAsNameOverriddenByUser = false;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("ui.toolbar.saveScene").c_str())) {
        scene.GetSceneDocument().environment = scene.GetSceneEnvironment();
        if (scene.SaveCurrentSceneDocument()) {
            context.sceneDirty = false;
        }
    }

    ImGui::Text(TEXT::Localize("ui.toolbar.sceneFormat").c_str(), scene.GetSceneDocument().sceneName.c_str(),
                context.sceneDirty ? "*" : "");
    ImGui::Text(TEXT::Localize("ui.toolbar.sceneAssetGuidFormat").c_str(),
                scene.GetCurrentSceneAssetGuid().IsValid() ? scene.GetCurrentSceneAssetGuid().value.c_str()
                                                           : TEXT::Localize("ui.common.transient").c_str());
    ImGui::Text(TEXT::Localize("ui.toolbar.objectsFormat").c_str(), scene.GetSceneDocument().objects.size());
#else
    (void)scene;
    (void)context;
    (void)selectionSync;
#endif
}

} // namespace HIKARI
