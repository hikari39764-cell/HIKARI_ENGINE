#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"

#include <algorithm>

#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "HIKARI_Services.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {

namespace {
#if defined(HIKARI_WITH_EDITOR)
constexpr float kTimelineHeightRatio = 0.25f;
constexpr float kRightColumnWidthRatio = 0.30f;
constexpr float kCameraPanelHeightRatio = 0.60f;

#endif
} // namespace

void CinematicsWorkspaceController::DrawDockSpace(
    bool resetDefaultDockLayout) const {
#if defined(HIKARI_WITH_EDITOR)
  ImGuiIO &io = ImGui::GetIO();
  if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
    return;
  }

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const ImGuiID dockspaceId = ImGui::GetID("HIKARI_CinematicsDockSpace_v4");
  const ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;
  const bool needsDefaultLayout =
      ImGui::DockBuilderGetNode(dockspaceId) == nullptr;
  if (needsDefaultLayout || resetDefaultDockLayout) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId,
                              ImGuiDockNodeFlags_DockSpace | dockspaceFlags);
    ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID contentNode = dockspaceId;
    ImGuiID timelineNode = 0;
    ImGui::DockBuilderSplitNode(contentNode, ImGuiDir_Down,
                                kTimelineHeightRatio, &timelineNode,
                                &contentNode);

    ImGuiID directorNode = contentNode;
    ImGuiID rightNode = 0;
    ImGuiID gameNode = 0;
    ImGuiID cameraNode = 0;
    ImGui::DockBuilderSplitNode(directorNode, ImGuiDir_Right,
                                kRightColumnWidthRatio, &rightNode,
                                &directorNode);
    ImGui::DockBuilderSplitNode(rightNode, ImGuiDir_Down,
                                kCameraPanelHeightRatio, &cameraNode,
                                &gameNode);

    ImGui::DockBuilderDockWindow("Director View###Cinematics/DirectorView",
                                 directorNode);
    ImGui::DockBuilderDockWindow("Game View###Cinematics/GameView", gameNode);
    ImGui::DockBuilderDockWindow("Map Overview###Cinematics/MapOverview",
                                 cameraNode);
    ImGui::DockBuilderDockWindow("Cameras###Cinematics/Cameras", cameraNode);
    ImGui::DockBuilderDockWindow("Timeline###Cinematics/Timeline",
                                 timelineNode);
    ImGui::DockBuilderFinish(dockspaceId);
  }
  ImGui::DockSpaceOverViewport(dockspaceId, viewport, dockspaceFlags);
#else
  (void)resetDefaultDockLayout;
#endif
}

CinematicsWorkspaceResult CinematicsWorkspaceController::Draw(
    DocumentSceneBase &scene, EditorPlaySession &playSession,
    EditorContext &context, SelectionSyncService &selectionSync,
    EditorWorkspaceHost &workspaceHost) {

  CinematicsWorkspaceResult result{};
  result.statusMessage.swap(pendingStatusMessage_);
#if defined(HIKARI_WITH_EDITOR)
  EditorViewInstance &gameView = workspaceHost.GetCinematicsGameView();
  if (scene.IsRuntimePlayActive()) {
    gameView.purpose = RENDER3D::RenderViewPurpose::Game;
    gameView.cameraBinding.kind = EditorViewCameraSourceKind::SceneDirector;
    gameView.cameraBinding.sceneObjectId = {};
  } else {
    gameView.purpose = RENDER3D::RenderViewPurpose::EditorScene;
    Camera3D resolvedBoundCamera{};
    if (boundCameraObjectId_ &&
        !scene.TryResolveCameraObjectView(*boundCameraObjectId_, 16.0f / 9.0f,
                                          resolvedBoundCamera)) {
      if (cameraPreviewOwned_) {
        scene.EndEditorCameraPreview();
      }
      cameraPreviewOwned_ = false;
      ClearCameraBinding(workspaceHost);
    }

    if (boundCameraObjectId_) {
      gameView.cameraBinding.kind =
          EditorViewCameraSourceKind::SceneCameraObject;
      gameView.cameraBinding.sceneObjectId = *boundCameraObjectId_;
      if (!scene.IsEditorCameraPreviewActive()) {
        cameraPreviewOwned_ =
            scene.BeginEditorCameraPreview(*boundCameraObjectId_);
        if (!cameraPreviewOwned_) {
          ClearCameraBinding(workspaceHost);
        }
      }
    } else {
      gameView.cameraBinding.kind =
          EditorViewCameraSourceKind::OwnedEditorCamera;
      gameView.cameraBinding.sceneObjectId = {};
    }
  }

  DrawTimelineWindow(scene, context, workspaceHost, result);
  DrawDirectorViewWindow(scene, context, selectionSync, workspaceHost);
  result.toggleGamePreviewRequested =
      DrawGameViewWindow(scene, playSession, workspaceHost);
  DrawMapOverviewWindow(scene, context, workspaceHost, result);
  DrawCameraListWindow(scene, context, workspaceHost, result);
#else
  (void)scene;
  (void)playSession;
  (void)context;
  (void)selectionSync;
  (void)workspaceHost;
#endif
  return result;
}

} // namespace HIKARI::EDITOR
