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

void CinematicsWorkspaceController::DrawMapOverviewWindow(
    DocumentSceneBase &scene, EditorContext &context,
    EditorWorkspaceHost &workspaceHost, CinematicsWorkspaceResult &result) {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Map Overview###Cinematics/MapOverview", nullptr,
                    ImGuiWindowFlags_NoCollapse)) {
    EditorViewInstance &view = workspaceHost.GetCinematicsOverviewView();
    view.extent = {};
    view.interaction = {};
    ImGui::End();
    return;
  }
  const CameraOverviewPanelResult panelResult =
      cameraOverviewPanel_.DrawOverviewContents(
          scene, context, workspaceHost.GetCinematicsOverviewView());
  ApplyCameraOverviewAction(scene, panelResult.action, context, workspaceHost,
                            result);
  ImGui::End();
#else
  (void)scene;
  (void)context;
  (void)workspaceHost;
  (void)result;
#endif
}

void CinematicsWorkspaceController::DrawCameraListWindow(
    DocumentSceneBase &scene, EditorContext &context,
    EditorWorkspaceHost &workspaceHost, CinematicsWorkspaceResult &result) {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Cameras###Cinematics/Cameras", nullptr,
                    ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }
  const CameraOverviewPanelResult panelResult =
      cameraOverviewPanel_.DrawCameraListContents(scene, context);
  ApplyCameraOverviewAction(scene, panelResult.action, context, workspaceHost,
                            result);
  ImGui::End();
#else
  (void)scene;
  (void)context;
  (void)workspaceHost;
  (void)result;
#endif
}

} // namespace HIKARI::EDITOR
