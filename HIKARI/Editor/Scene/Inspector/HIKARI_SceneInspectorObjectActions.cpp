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

namespace {
bool HasDocumentComponent(const SceneObjectData &object,
                          std::string_view typeName) {
  return std::any_of(object.components.begin(), object.components.end(),
                     [typeName](const SceneComponentData &component) {
                       return component.type == typeName;
                     });
}
} // namespace

void SceneInspectorPanel::DrawObjectContextMenu(
    DocumentSceneBase &scene, EditorContext &context,
    const SelectionSyncService &selectionSync,
    SceneObjectCommandService &commands, GameObject &object) {
#if defined(HIKARI_WITH_EDITOR)
  if (context.selection.selectedObject != &object) {
    if (World *world = object.GetWorld()) {
      context.selection.SelectObject(
          *world, &object,
          context.selection.IsObjectSelected(object.GetDocumentId())
              ? EditorObjectSelectionMode::Add
              : EditorObjectSelectionMode::Replace);
    }
  }
  SceneObjectData *target =
      selectionSync.FindDocumentObjectByRuntime(scene, &object);
  if (target == nullptr) {
    ImGui::TextDisabled("Object no longer exists");
    return;
  }

  const auto commandItem = [&](SceneObjectCommandId command) {
    const SceneObjectCommandPresentation &presentation =
        GetSceneObjectCommandPresentation(command);
    return ImGui::MenuItem(presentation.label, presentation.shortcut, false,
                           commands.CanExecute(command, scene, context));
  };

  if (commandItem(SceneObjectCommandId::Rename)) {
    RequestRename(object);
  }
  const SceneObjectCommandId lockCommand = target->editorLocked
                                               ? SceneObjectCommandId::Unlock
                                               : SceneObjectCommandId::Lock;
  if (commandItem(lockCommand)) {
    commands.Execute(lockCommand, scene, context, selectionSync);
    return;
  }
  ImGui::Separator();
  if (commandItem(SceneObjectCommandId::Duplicate)) {
    commands.Execute(SceneObjectCommandId::Duplicate, scene, context,
                     selectionSync);
    return;
  }
  if (commandItem(SceneObjectCommandId::Delete)) {
    commands.Execute(SceneObjectCommandId::Delete, scene, context,
                     selectionSync);
    return;
  }

  ImGui::Separator();
  if (ImGui::BeginMenu("Edit")) {
    if (ImGui::MenuItem("Focus", "F")) {
      focusObjectRequest_ = target->id;
    }
    if (ImGui::MenuItem("Add Component...", nullptr, false,
                        !target->editorLocked)) {
      openComponentPicker_ = true;
    }
    if (ImGui::MenuItem("Collision...", nullptr, false,
                        !target->editorLocked)) {
      collisionAuthoringDialog_.Open(target->id);
    }
    ImGui::EndMenu();
  }

  if (ImGui::BeginMenu("Prefab")) {
    if (ImGui::MenuItem("Save as Prefab...", nullptr, false,
                        commands.CanExecute(SceneObjectCommandId::SaveAsPrefab,
                                            scene, context))) {
      prefabObjectId_ = target->id;
      std::snprintf(prefabBuffer_, sizeof(prefabBuffer_), "%s",
                    target->name.c_str());
      openPrefabPopup_ = true;
    }
    const std::vector<std::string> prefabIds = commands.ListPrefabIds();
    if (!prefabIds.empty()) {
      ImGui::Separator();
      if (ImGui::BeginMenu("Instantiate")) {
        for (const std::string &prefabId : prefabIds) {
          if (ImGui::MenuItem(prefabId.c_str())) {
            commands.Execute(SceneObjectCommandId::InstantiatePrefab, scene,
                             context, selectionSync, prefabId);
          }
        }
        ImGui::EndMenu();
      }
    }
    ImGui::EndMenu();
  }

  if (HasDocumentComponent(*target, "CameraComponent") &&
      ImGui::BeginMenu("Camera")) {
    const bool isDefault =
        scene.GetSceneDocument().camera.defaultCameraObjectId == target->id;
    if (ImGui::MenuItem("Set as Game Default", nullptr, isDefault,
                        !isDefault)) {
      if (scene.SetGameDefaultCamera(target->id)) {
        context.sceneDirty = true;
      }
    }
    const bool previewing =
        scene.IsEditorCameraPreviewActive() &&
        scene.GetEditorCameraPreviewObjectId() == target->id;
    if (ImGui::MenuItem(previewing ? "Exit Camera View"
                                   : "View Through Camera")) {
      if (previewing) {
        scene.EndEditorCameraPreview();
      } else {
        (void)scene.BeginEditorCameraPreview(target->id);
      }
    }
    if (ImGui::MenuItem("Snap Camera to Current View", nullptr, false,
                        !target->editorLocked && !target->parent.has_value())) {
      if (scene.SnapCameraObjectToEditorView(target->id)) {
        context.sceneDirty = true;
      }
    }
    if (ImGui::MenuItem("Open Cinematics Workspace")) {
      openCinematicsWorkspaceCameraRequest_ = target->id;
    }
    ImGui::EndMenu();
  }
#else
  (void)scene;
  (void)context;
  (void)selectionSync;
  (void)commands;
  (void)object;
#endif
}

} // namespace HIKARI::EDITOR
