#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"

#include <algorithm>

#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmoShortcuts.h"
#include "Editor/HIKARI_EditorContext.h"
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

void CinematicsWorkspaceController::DrawDirectorViewWindow(
    DocumentSceneBase &scene, EditorContext &context,
    SelectionSyncService &selectionSync, EditorWorkspaceHost &workspaceHost) {
#if defined(HIKARI_WITH_EDITOR)
  EditorViewInstance &view = workspaceHost.GetCinematicsDirectorView();
  constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoScrollWithMouse |
                                     ImGuiWindowFlags_NoCollapse;
  if (!ImGui::Begin("Director View###Cinematics/DirectorView", nullptr,
                    flags)) {
    view.extent = {};
    view.interaction = {};
    RENDER3D::EDITORVIEW::ClearRequest(view.renderViewId);
    ImGui::End();
    return;
  }

  const bool focused =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  HandleTransformGizmoShortcuts(context.transformGizmo, focused);
  const SceneObjectId selectedObjectId =
      context.selection.selectedObject
          ? context.selection.selectedObject->GetDocumentId()
          : SceneObjectId{};
  const DirectorViewPanelResult result = directorViewPanel_.DrawContents(
      scene, view, selectedObjectId, context.transformGizmo,
      TIME::GetFrameContext().unscaledDt, scene.IsRuntimePlayActive());
  ApplyDirectorViewResult(scene, result, context, selectionSync);
  ImGui::End();
#else
  (void)scene;
  (void)context;
  (void)selectionSync;
  (void)workspaceHost;
#endif
}

} // namespace HIKARI::EDITOR
