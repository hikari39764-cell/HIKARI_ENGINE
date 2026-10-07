#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Editor/Scene/Inspector/HIKARI_SceneInspectorPanel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "Editor/Commands/HIKARI_SceneObjectCommandService.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void SceneInspectorPanel::DrawDeferredDialogs(DocumentSceneBase& scene, EditorContext& context,
                                              const SelectionSyncService& selectionSync,
                                              SceneObjectCommandService& commands) {
#if defined(HIKARI_WITH_EDITOR)
    DrawAddComponentPopup(scene, context, selectionSync, commands);

    if (openRenamePopup_) {
        ImGui::OpenPopup(TEXT::LocalizeWindow("editor.ui.rename.object.0c3276", "Rename Object").c_str());
        openRenamePopup_ = false;
    }
    if (ImGui::BeginPopupModal(TEXT::LocalizeWindow("editor.ui.rename.object.0c3276", "Rename Object").c_str(), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(320.0f);
        const bool submit =
            ImGui::InputText("##Name", renameBuffer_, sizeof(renameBuffer_), ImGuiInputTextFlags_EnterReturnsTrue);
        if (submit || ImGui::Button(TEXT::LocalizeWindow("editor.rename", "Rename").c_str())) {
            if (context.selection.selectedObject != nullptr &&
                context.selection.selectedObject->GetDocumentId() == renameObjectId_) {
                commands.Execute(SceneObjectCommandId::Rename, scene, context, selectionSync, renameBuffer_);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.common.cancel", "Cancel").c_str())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (openPrefabPopup_) {
        ImGui::OpenPopup(TEXT::LocalizeWindow("editor.ui.save.as.prefab.13bd13", "Save as Prefab").c_str());
        openPrefabPopup_ = false;
    }
    if (ImGui::BeginPopupModal(TEXT::LocalizeWindow("editor.ui.save.as.prefab.13bd13", "Save as Prefab").c_str(),
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(320.0f);
        const bool submit = ImGui::InputText("##PrefabName", prefabBuffer_, sizeof(prefabBuffer_),
                                             ImGuiInputTextFlags_EnterReturnsTrue);
        if (submit || ImGui::Button(TEXT::LocalizeWindow("ai.api.save", "Save").c_str())) {
            if (context.selection.selectedObject != nullptr &&
                context.selection.selectedObject->GetDocumentId() == prefabObjectId_) {
                commands.Execute(SceneObjectCommandId::SaveAsPrefab, scene, context, selectionSync, prefabBuffer_);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.common.cancel", "Cancel").c_str())) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    CollisionAuthoringRequest collisionRequest{};
    if (collisionAuthoringDialog_.Draw(collisionRequest)) {
        const bool dirtyBefore = context.sceneDirty || scene.HasUnsavedSceneChanges();
        const std::vector<SceneObjectData> beforeObjects = scene.GetSceneDocument().objects;
        const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
        const CollisionAuthoringResult result =
            collisionAuthoringService_.Apply(scene.GetComponentRegistry(), scene.GetSceneDocument(), collisionRequest);
        context.componentAddStatusMessage = result.message;
        context.componentAddStatusIsError = !result.success;
        if (result.documentChanged) {
            context.sceneDirty = true;
            selectionSync.ApplyRuntimeObjectChangesWithSelectionSync(
                scene, context.selection, context.nextSceneObjectId,
                DiffSceneObjects(beforeObjects, scene.GetSceneDocument().objects));
            historyRequest_ = SceneObjectAuthoringHistoryRequest{
                collisionRequest.scope == CollisionAuthoringScope::SceneGeometry ? "Setup Scene Collision"
                                                                                 : "Setup Object Collision",
                beforeObjects,
                scene.GetSceneDocument().objects,
                beforeCamera,
                scene.GetSceneDocument().camera,
                dirtyBefore};
        }
    }
#else
    (void)scene;
    (void)context;
    (void)selectionSync;
    (void)commands;
#endif
}

} // namespace HIKARI::EDITOR
