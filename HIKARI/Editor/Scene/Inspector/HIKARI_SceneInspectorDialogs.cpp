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
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void SceneInspectorPanel::DrawDeferredDialogs(
    DocumentSceneBase &scene, EditorContext &context,
    const SelectionSyncService &selectionSync,
    SceneObjectCommandService &commands) {
#if defined(HIKARI_WITH_EDITOR)
  DrawAddComponentPopup(scene, context, selectionSync, commands);

  if (openRenamePopup_) {
    ImGui::OpenPopup("Rename Object");
    openRenamePopup_ = false;
  }
  if (ImGui::BeginPopupModal("Rename Object", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::SetNextItemWidth(320.0f);
    const bool submit =
        ImGui::InputText("##Name", renameBuffer_, sizeof(renameBuffer_),
                         ImGuiInputTextFlags_EnterReturnsTrue);
    if (submit || ImGui::Button("Rename")) {
      if (context.selection.selectedObject != nullptr &&
          context.selection.selectedObject->GetDocumentId() ==
              renameObjectId_) {
        commands.Execute(SceneObjectCommandId::Rename, scene, context,
                         selectionSync, renameBuffer_);
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (openPrefabPopup_) {
    ImGui::OpenPopup("Save as Prefab");
    openPrefabPopup_ = false;
  }
  if (ImGui::BeginPopupModal("Save as Prefab", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::SetNextItemWidth(320.0f);
    const bool submit =
        ImGui::InputText("##PrefabName", prefabBuffer_, sizeof(prefabBuffer_),
                         ImGuiInputTextFlags_EnterReturnsTrue);
    if (submit || ImGui::Button("Save")) {
      if (context.selection.selectedObject != nullptr &&
          context.selection.selectedObject->GetDocumentId() ==
              prefabObjectId_) {
        commands.Execute(SceneObjectCommandId::SaveAsPrefab, scene, context,
                         selectionSync, prefabBuffer_);
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  CollisionAuthoringRequest collisionRequest{};
  if (collisionAuthoringDialog_.Draw(collisionRequest)) {
    const bool dirtyBefore =
        context.sceneDirty || scene.HasUnsavedSceneChanges();
    const std::vector<SceneObjectData> beforeObjects =
        scene.GetSceneDocument().objects;
    const SceneCameraSettings beforeCamera = scene.GetSceneDocument().camera;
    const CollisionAuthoringResult result = collisionAuthoringService_.Apply(
        scene.GetComponentRegistry(), scene.GetSceneDocument(),
        collisionRequest);
    context.componentAddStatusMessage = result.message;
    context.componentAddStatusIsError = !result.success;
    if (result.documentChanged) {
      context.sceneDirty = true;
      selectionSync.RebuildRuntimeWorldWithSelectionSync(
          scene, context.selection, context.nextSceneObjectId);
      historyRequest_ = SceneObjectAuthoringHistoryRequest{
          collisionRequest.scope == CollisionAuthoringScope::SceneGeometry
              ? "Setup Scene Collision"
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
