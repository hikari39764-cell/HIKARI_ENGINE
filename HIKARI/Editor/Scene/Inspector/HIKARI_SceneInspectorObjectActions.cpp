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
bool HasDocumentComponent(const SceneObjectData& object, std::string_view typeName) {
    return std::any_of(object.components.begin(), object.components.end(),
                       [typeName](const SceneComponentData& component) { return component.type == typeName; });
}
} // namespace

void SceneInspectorPanel::ExecutePendingObjectCommand(DocumentSceneBase& scene, EditorContext& context,
                                                      const SelectionSyncService& selectionSync,
                                                      SceneObjectCommandService& commands) {
    // Game View は Scene 以外にも存在するので、共通の UI フレーム末尾で消費する。
    if (auto pending = std::exchange(pendingObjectCommand_, std::nullopt);
        pending && pending->scene == &scene && pending->documentRevision == scene.GetSceneDocumentRevision() &&
        pending->selection == context.selection.GetSelectedObjectIds()) {
        commands.Execute(pending->command, scene, context, selectionSync, pending->argument);
    }
}

void SceneInspectorPanel::DrawObjectContextMenu(DocumentSceneBase& scene, EditorContext& context,
                                                const SelectionSyncService& selectionSync,
                                                SceneObjectCommandService& commands, GameObject& object) {
#if defined(HIKARI_WITH_EDITOR)
    if (context.selection.selectedObject != &object) {
        if (World* world = object.GetWorld()) {
            context.selection.SelectObject(*world, &object,
                                           context.selection.IsObjectSelected(object.GetDocumentId())
                                               ? EditorObjectSelectionMode::Add
                                               : EditorObjectSelectionMode::Replace);
        }
    }
    SceneObjectData* target = selectionSync.FindDocumentObjectByRuntime(scene, &object);
    if (target == nullptr) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.object.no.longer.exists.6678ef").c_str());
        return;
    }

    const auto commandItem = [&](SceneObjectCommandId command) {
        const SceneObjectCommandPresentation& presentation = GetSceneObjectCommandPresentation(command);
        return ImGui::MenuItem(presentation.label, presentation.shortcut, false,
                               commands.CanExecute(command, scene, context));
    };
    // 階層・Inspector の借用ポインターが生存する間は World を再構築しない。
    const auto deferCommand = [&](SceneObjectCommandId command, std::string argument = {}) {
        pendingObjectCommand_ =
            PendingObjectCommand{command, std::move(argument), &scene, scene.GetSceneDocumentRevision(),
                                 context.selection.GetSelectedObjectIds()};
    };

    if (commandItem(SceneObjectCommandId::Rename)) {
        RequestRename(object);
    }
    const SceneObjectCommandId lockCommand =
        target->editorLocked ? SceneObjectCommandId::Unlock : SceneObjectCommandId::Lock;
    if (commandItem(lockCommand)) {
        commands.Execute(lockCommand, scene, context, selectionSync);
        return;
    }
    ImGui::Separator();
    if (commandItem(SceneObjectCommandId::Duplicate)) {
        deferCommand(SceneObjectCommandId::Duplicate);
        return;
    }
    if (commandItem(SceneObjectCommandId::Delete)) {
        deferCommand(SceneObjectCommandId::Delete);
        return;
    }

    ImGui::Separator();
    if (ImGui::BeginMenu(TEXT::LocalizeWindow("menu.edit", "Edit").c_str())) {
        if (ImGui::MenuItem(TEXT::LocalizeWindow("ui.vfx.text.Focus", "Focus").c_str(), "F")) {
            RequestFocusObject(target->id);
        }
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.add.component.08ba89", "Add Component...").c_str(), nullptr,
                            false, !target->editorLocked)) {
            openComponentPicker_ = true;
        }
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.collision.8d538c", "Collision...").c_str(), nullptr, false,
                            !target->editorLocked)) {
            collisionAuthoringDialog_.Open(target->id);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(TEXT::LocalizeWindow("ui.sceneCreation.prefab", "Prefab").c_str())) {
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.save.as.prefab.5cb805", "Save as Prefab...").c_str(),
                            nullptr, false, commands.CanExecute(SceneObjectCommandId::SaveAsPrefab, scene, context))) {
            prefabObjectId_ = target->id;
            std::snprintf(prefabBuffer_, sizeof(prefabBuffer_), "%s", target->name.c_str());
            openPrefabPopup_ = true;
        }
        const std::vector<PrefabAssetEntry> prefabs = commands.ListPrefabs(scene.GetAssetDatabase());
        if (!prefabs.empty()) {
            ImGui::Separator();
            if (ImGui::BeginMenu(TEXT::LocalizeWindow("editor.ui.instantiate.bad193", "Instantiate").c_str())) {
                for (const PrefabAssetEntry& prefab : prefabs) {
                    if (ImGui::MenuItem(prefab.name.c_str())) {
                        deferCommand(SceneObjectCommandId::InstantiatePrefab, prefab.guid.value);
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndMenu();
    }

    if (HasDocumentComponent(*target, "CameraComponent") &&
        ImGui::BeginMenu(TEXT::LocalizeWindow("ui.inspector.category.camera", "Camera").c_str())) {
        const bool isDefault = scene.GetSceneDocument().camera.defaultCameraObjectId == target->id;
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.set.as.game.default.47a702", "Set as Game Default").c_str(),
                            nullptr, isDefault, !isDefault)) {
            if (scene.SetGameDefaultCamera(target->id)) {
                context.sceneDirty = true;
            }
        }
        const bool previewing =
            scene.IsEditorCameraPreviewActive() && scene.GetEditorCameraPreviewObjectId() == target->id;
        if (ImGui::MenuItem(previewing ? TEXT::LocalizeCStr("editor.ui.exit.camera.view.32714d")
                                       : TEXT::LocalizeCStr("editor.ui.view.through.camera.2e4994"))) {
            if (previewing) {
                scene.EndEditorCameraPreview();
            } else {
                (void)scene.BeginEditorCameraPreview(target->id);
            }
        }
        if (ImGui::MenuItem(
                TEXT::LocalizeWindow("editor.ui.snap.camera.to.current.view.aab29b", "Snap Camera to Current View")
                    .c_str(),
                nullptr, false, !target->editorLocked && !target->parent.has_value())) {
            if (scene.SnapCameraObjectToEditorView(target->id)) {
                context.sceneDirty = true;
            }
        }
        if (ImGui::MenuItem(
                TEXT::LocalizeWindow("editor.ui.open.cinematics.workspace.11dde1", "Open Cinematics Workspace")
                    .c_str())) {
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
