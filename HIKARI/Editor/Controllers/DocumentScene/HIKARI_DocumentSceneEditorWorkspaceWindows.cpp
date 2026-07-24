#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Editor/Authoring/HIKARI_EditorObjectState.h"
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
        if (!ImGui::Begin("Scene Workspace")) {
            ImGui::End();
            return;
        }

        EDITOR::PanelTitle("Objects");
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
                "Create Object")) {
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
            });

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
        if (!ImGui::Begin("Diagnostics")) {
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("DebugWorkspaceTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
            if (ImGui::BeginTabItem("Stats")) {
                ImGui::SeparatorText("Frame Overview");
                statsPanel_.DrawContents(scene.GetSceneName(), scene.GetWorld(), scene.GetModelManager(), context_.selection, scene.GetCamera());
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Time")) {
                ImGui::SeparatorText("Timeline");
                timePanel_.DrawContents();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Runtime Refresh")) {
                const RuntimeResourceRefreshReport& report = context_.lastRuntimeRefreshReport;
                ImGui::SeparatorText("Runtime Resource Refresh");
                ImGui::Text("Texture invalidated: %d", report.textureInvalidatedCount);
                ImGui::Text("Sky refreshed: %d", report.skyInvalidatedCount);
                ImGui::Text("Model reloaded: %d", report.modelReloadedCount);
                ImGui::Text("Components rebound: %d", report.modelReboundComponentCount);
                ImGui::Text("Failed: %d", report.failedCount);
                ImGui::SeparatorText("Messages");
                if (report.messages.empty()) {
                    ImGui::TextDisabled("No runtime refresh has run yet.");
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
