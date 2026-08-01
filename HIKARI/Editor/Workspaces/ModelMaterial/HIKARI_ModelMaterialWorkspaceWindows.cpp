#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::DrawDockSpace(
    bool resetDefaultDockLayout) const {
#if defined(HIKARI_WITH_EDITOR)
  ImGuiIO &io = ImGui::GetIO();
  if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
    return;
  }
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const ImGuiID dockspaceId = ImGui::GetID("HIKARI_ModelMaterialDockSpace_v2");
  if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr ||
      resetDefaultDockLayout) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID center = dockspaceId;
    ImGuiID left = 0u;
    ImGuiID right = 0u;
    ImGuiID bottom = 0u;
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.21f, &left, &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.29f, &right,
                                &center);
    ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.38f, &bottom,
                                &center);

    ImGui::DockBuilderDockWindow("Preview###ModelMaterial/Preview", center);
    ImGui::DockBuilderDockWindow(
        "Models & Material Slots###ModelMaterial/Slots", left);
    ImGui::DockBuilderDockWindow("Binding###ModelMaterial/Binding", right);
    ImGui::DockBuilderDockWindow("Material###ModelMaterial/Material", right);
    ImGui::DockBuilderDockWindow("Instance###ModelMaterial/Instance", right);
    ImGui::DockBuilderDockWindow("Ramp###ModelMaterial/Ramp", right);
    ImGui::DockBuilderDockWindow("Material Graph###ModelMaterial/Graph",
                                 bottom);
    ImGui::DockBuilderDockWindow("Diagnostics###ModelMaterial/Diagnostics",
                                 bottom);
    ImGui::DockBuilderDockWindow("Stats###ModelMaterial/Stats", bottom);
    ImGui::DockBuilderFinish(dockspaceId);
  }
  ImGui::DockSpaceOverViewport(dockspaceId, viewport);
#else
  (void)resetDefaultDockLayout;
#endif
}

ModelMaterialWorkspaceResult ModelMaterialWorkspaceController::Draw(
    DocumentSceneBase &scene, EditorWorkspaceHost &workspaceHost,
    EditorCommandRouter &commandRouter) {
  ModelMaterialWorkspaceResult result{};
  result.statusMessage = statusMessage_;
#if defined(HIKARI_WITH_EDITOR)
  RefreshAssetChoices(scene.GetAssetDatabase());
  if (materialGesture_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    CancelMaterialGesture(scene);
  } else if (authoringDocument_.IsAbPreviewActive() &&
             !ImGui::GetIO().WantTextInput &&
             ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    if (authoringDocument_.DiscardAbPreview(statusMessage_)) {
      (void)RebuildAuthoredPreview(scene.GetAssetDatabase(),
                                   scene.GetAssetRegistry(), statusMessage_);
    }
  }
  DrawPreviewWindow(scene, workspaceHost, result, commandRouter);
  DrawModelAndSlotsWindow(scene);
  DrawBindingWindow(scene);
  DrawMaterialWindow(scene);
  DrawInstanceWindow(scene);
  DrawGraphWindow(scene);
  DrawRampWindow(scene);
  DrawDiagnosticsWindow(scene);
  DrawStatsWindow();
#else
  (void)scene;
  (void)workspaceHost;
  (void)commandRouter;
#endif
  result.statusMessage = statusMessage_;
  return result;
}

} // namespace HIKARI::EDITOR
