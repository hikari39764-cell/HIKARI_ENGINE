#include "Editor/Panels/HIKARI_SceneCreationPanel.h"

#include "Editor/Authoring/HIKARI_EditorObjectFactory.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    void SceneCreationPanel::DrawUiCreationMenu(
        DocumentSceneBase& scene,
        EditorContext& context,
        const SelectionSyncService& selectionSync,
        std::string_view search) {
#if defined(HIKARI_WITH_EDITOR)
        if (scene.GetComponentRegistry().Find("UiCanvasComponent") == nullptr) {
            return;
        }
        const std::string uiLabel = TEXT::Localize("ui.sceneCreation.ui");
        const std::string canvasLabel =
            TEXT::Localize("ui.sceneCreation.canvas");
        const std::string buttonLabel =
            TEXT::Localize("ui.sceneCreation.button");
        const std::string sceneButtonLabel =
            TEXT::Localize("ui.sceneCreation.sceneButton");
        const bool matchesUi = MatchesSearch(uiLabel, search) ||
            MatchesSearch(canvasLabel, search) ||
            MatchesSearch(buttonLabel, search) ||
            MatchesSearch(sceneButtonLabel, search);
        if (!matchesUi || !ImGui::BeginMenu(uiLabel.c_str())) return;
        if ((search.empty() || MatchesSearch(canvasLabel, search)) &&
            ImGui::MenuItem(canvasLabel.c_str())) {
            CreateUiObject(scene, context, selectionSync, false);
        }
        if ((search.empty() || MatchesSearch(buttonLabel, search)) &&
            ImGui::MenuItem(buttonLabel.c_str())) {
            CreateUiObject(scene, context, selectionSync, true, false);
        }
        if (scene.GetComponentRegistry().Find(
                "HIKARI.SceneFlow.Trigger") != nullptr &&
            (search.empty() || MatchesSearch(sceneButtonLabel, search)) &&
            ImGui::MenuItem(sceneButtonLabel.c_str())) {
            CreateUiObject(scene, context, selectionSync, true, true);
        }
        ImGui::EndMenu();
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
        (void)search;
#endif
    }

    void SceneCreationPanel::CreateUiObject(
        DocumentSceneBase& scene,
        EditorContext& context,
        const SelectionSyncService& selectionSync,
        bool button,
        bool addSceneFlow) {
        const bool dirtyBefore =
            context.sceneDirty || scene.HasUnsavedSceneChanges();
        const std::vector<SceneObjectData> beforeObjects =
            scene.GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera =
            scene.GetSceneDocument().camera;
        CreateObjectRequest request{};
        request.name = addSceneFlow
            ? "Scene Button"
            : button ? "UI Button" : "UI Canvas";
        GameObject* object = button
            ? EDITOR::CreateUiButtonObject(scene, request, addSceneFlow)
            : EDITOR::CreateUiCanvasObject(scene, request);
        if (scene.GetSceneDocument().objects.size() == beforeObjects.size()) {
            return;
        }
        if (object != nullptr) {
            context.selection.SelectObject(scene.GetWorld(), object);
        }
        context.sceneDirty = true;
        selectionSync.SyncNextSceneObjectId(
            scene, context.nextSceneObjectId);
        historyRequest_ = SceneObjectAuthoringHistoryRequest{
            addSceneFlow
                ? "Create UI Scene Button"
                : button ? "Create UI Button" : "Create UI Canvas",
            beforeObjects,
            scene.GetSceneDocument().objects,
            beforeCamera,
            scene.GetSceneDocument().camera,
            dirtyBefore
        };
    }

} // namespace HIKARI::EDITOR
