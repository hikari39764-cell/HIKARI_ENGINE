#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Editor/Authoring/HIKARI_EditorObjectState.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Runtime/HIKARI_RuntimeResourceRefreshService.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include <algorithm>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    void DocumentSceneEditorController::DrawSceneWorkspaceWindow(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin(
                TEXT::LocalizeWindow(
                    "ui.window.sceneWorkspace",
                    "Scene Workspace").c_str())) {
            ImGui::End();
            return;
        }

        EDITOR::PanelTitle(
            TEXT::Localize("ui.sceneWorkspace.objects").c_str());
        ImGui::SameLine();
        const float createButtonWidth = ImGui::GetFrameHeight();
        ImGui::SetCursorPosX((std::max)(
            ImGui::GetCursorPosX(),
            ImGui::GetWindowContentRegionMax().x - createButtonWidth));
        if (EDITOR::IconButton(
                EDITOR::EditorGlyph::Add,
                "CreateSceneObject",
                EDITOR::EditorButtonTone::Quiet,
                ImVec2(createButtonWidth, createButtonWidth),
                TEXT::Localize(
                    "ui.sceneWorkspace.createObject").c_str())) {
            ImGui::OpenPopup("SceneHierarchyCreateMenu");
        }
        if (ImGui::BeginPopup("SceneHierarchyCreateMenu")) {
            sceneCreationPanel_.DrawCreationMenu(
                scene,
                context_,
                selectionSync_,
                sceneObjectCommands_);
            ImGui::EndPopup();
        }

        hierarchyPanel_.DrawContents(
            scene.GetWorld(),
            context_.selection,
            [&](GameObject& object) {
                sceneInspectorPanel_.DrawObjectContextMenu(
                    scene,
                    context_,
                    selectionSync_,
                    sceneObjectCommands_,
                    object);
            },
            [&](const GameObject& object) {
                return EDITOR::IsObjectEditorLocked(
                    scene.GetSceneDocument(),
                    object.GetDocumentId());
            }, &scene.GetSceneDocument(), &operationRegistry_, scene.GetSceneDocumentRevision());

        if (ImGui::BeginPopupContextWindow(
                "SceneHierarchyEmptyContext",
                ImGuiPopupFlags_MouseButtonRight |
                    ImGuiPopupFlags_NoOpenOverItems)) {
            sceneCreationPanel_.DrawCreationMenu(
                scene,
                context_,
                selectionSync_,
                sceneObjectCommands_);
            ImGui::EndPopup();
        }

        if (!scene.IsRuntimePlayActive()) (void)commandRouter_.ProcessViewportShortcuts(
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
        ImGui::End();
#else
        (void)scene;
#endif
    }

    void DocumentSceneEditorController::DrawInspectorWindow(
        DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        sceneInspectorPanel_.Draw(
            scene,
            context_,
            selectionSync_,
            sceneObjectCommands_,
            &context_.windows.authoring.showInspector);
#else
        (void)scene;
#endif
    }

    void DocumentSceneEditorController::DrawDebugWorkspaceWindow(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin(TEXT::LocalizeWindow("shell.diagnostics", "Diagnostics").c_str())) {
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("DebugWorkspaceTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
            if (ImGui::BeginTabItem(TEXT::LocalizeWindow("editor.ui.stats.be763e", "Stats").c_str())) {
                ImGui::SeparatorText(TEXT::Localize("editor.ui.frame.overview.845bc3").c_str());
                statsPanel_.DrawContents(scene.GetSceneName(), scene.GetWorld(), scene.GetModelManager(), context_.selection, scene.GetCamera());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(TEXT::LocalizeWindow("ui.vfx.text.Time", "Time").c_str())) {
                ImGui::SeparatorText(TEXT::Localize("ui.vfx.text.Timeline").c_str());
                timePanel_.DrawContents();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(TEXT::LocalizeWindow("editor.ui.runtime.refresh.d87a71", "Runtime Refresh").c_str())) {
                const RuntimeResourceRefreshReport& report = context_.lastRuntimeRefreshReport;
                ImGui::SeparatorText(TEXT::Localize("editor.ui.runtime.resource.refresh.42a1a4").c_str());
                ImGui::Text(TEXT::Localize("editor.ui.texture.invalidated.d.fcaed4").c_str(), report.textureInvalidatedCount);
                ImGui::Text(TEXT::Localize("editor.ui.sky.refreshed.d.121c27").c_str(), report.skyInvalidatedCount);
                ImGui::Text(TEXT::Localize("editor.ui.model.reloaded.d.d9f643").c_str(), report.modelReloadedCount);
                ImGui::Text(TEXT::Localize("editor.ui.components.rebound.d.1746f0").c_str(), report.modelReboundComponentCount);
                ImGui::Text(TEXT::Localize("editor.ui.failed.d.16c1a4").c_str(), report.failedCount);
                ImGui::SeparatorText(TEXT::Localize("ui.lightingBake.messages").c_str());
                if (report.messages.empty()) {
                    ImGui::TextDisabled(TEXT::Localize("editor.ui.no.runtime.refresh.has.run.yet.92212b").c_str());
                } else {
                    for (const std::string& message : report.messages) {
                        ImGui::BulletText("%s", message.c_str());
                    }
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
#else
        (void)scene;
#endif
    }

} // namespace HIKARI
