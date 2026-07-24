#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceController.h"

#include "Core/HIKARI_TimeService.h"
#include "Editor/Views/HIKARI_EditorViewInputGate.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionViewportOverlay.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceInteraction.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include <algorithm>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {

void ModelCollisionWorkspaceController::DrawDockSpace(
    bool resetDefaultDockLayout) const {
#if defined(HIKARI_WITH_EDITOR)
  ImGuiIO &io = ImGui::GetIO();
  if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
    return;
  }
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const ImGuiID dockspaceId = ImGui::GetID("HIKARI_ModelCollisionDockSpace_v1");
  const bool needsLayout = ImGui::DockBuilderGetNode(dockspaceId) == nullptr;
  if (needsLayout || resetDefaultDockLayout) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID previewNode = dockspaceId;
    ImGuiID leftNode = 0;
    ImGuiID rightNode = 0;
    ImGuiID leftBottom = 0;
    ImGuiID rightBottom = 0;
    ImGui::DockBuilderSplitNode(previewNode, ImGuiDir_Left, 0.22f, &leftNode,
                                &previewNode);
    ImGui::DockBuilderSplitNode(previewNode, ImGuiDir_Right, 0.27f, &rightNode,
                                &previewNode);
    ImGui::DockBuilderSplitNode(leftNode, ImGuiDir_Down, 0.48f, &leftBottom,
                                &leftNode);
    ImGui::DockBuilderSplitNode(rightNode, ImGuiDir_Down, 0.45f, &rightBottom,
                                &rightNode);

    ImGui::DockBuilderDockWindow("Model Preview###ModelCollision/Preview",
                                 previewNode);
    ImGui::DockBuilderDockWindow("Collision Shapes###ModelCollision/Shapes",
                                 leftNode);
    ImGui::DockBuilderDockWindow("Source Model###ModelCollision/Source",
                                 leftBottom);
    ImGui::DockBuilderDockWindow("Shape Details###ModelCollision/Details",
                                 rightNode);
    ImGui::DockBuilderDockWindow("Auto Generate###ModelCollision/Generate",
                                 rightBottom);
    ImGui::DockBuilderFinish(dockspaceId);
  }
  ImGui::DockSpaceOverViewport(dockspaceId, viewport);
#else
  (void)resetDefaultDockLayout;
#endif
}

ModelCollisionWorkspaceResult
ModelCollisionWorkspaceController::Draw(DocumentSceneBase &scene,
                                        EditorWorkspaceHost &workspaceHost,
                                        EditorCommandRouter &commandRouter) {

  ModelCollisionWorkspaceResult result{};
  result.statusMessage = statusMessage_;
#if defined(HIKARI_WITH_EDITOR)
  assetTaskService_ = &scene.GetAssetDatabase().GetAssetTaskService();
  BindSelectionCommands(commandRouter);
  DrawPreviewWindow(scene, workspaceHost, result, commandRouter);
  DrawShapeListWindow();
  DrawShapeDetailsWindow();
  DrawSourceModelWindow();
  DrawAutoGenerateWindow();
  DrawPendingModelOpenModal(scene, result);
  DrawPendingCloseModal(scene, result);
#else
  (void)scene;
  (void)workspaceHost;
  (void)commandRouter;
#endif
  return result;
}

} // namespace HIKARI::EDITOR
