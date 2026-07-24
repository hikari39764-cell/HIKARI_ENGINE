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

bool CinematicsWorkspaceController::DrawGameViewWindow(
    DocumentSceneBase &scene, EditorPlaySession &playSession,
    EditorWorkspaceHost &workspaceHost) {

  bool toggleRequested = false;
#if defined(HIKARI_WITH_EDITOR)
  EditorViewInstance &view = workspaceHost.GetCinematicsGameView();
  constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoScrollWithMouse |
                                     ImGuiWindowFlags_NoCollapse;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  if (!ImGui::Begin("Game View###Cinematics/GameView", nullptr, flags)) {
    view.extent = {};
    view.interaction = {};
    ClearGameViewportInputRect();
    SERVICES::SetEditorGameViewportSize(0, 0, false);
    ImGui::End();
    ImGui::PopStyleVar();
    return false;
  }

  view.interaction.visible = true;
  constexpr float toolbarHeight = 36.0f;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.055f, 0.065f, 0.080f, 1.0f));
  if (ImGui::BeginChild("##CinematicsGameToolbar", ImVec2(0.0f, toolbarHeight),
                        false, ImGuiWindowFlags_NoScrollbar)) {
    ImGui::SetCursorPos(ImVec2(8.0f, 7.0f));
    const bool previewRunning = playSession.IsRunning();
    if (IconButton(previewRunning ? EditorGlyph::Stop : EditorGlyph::Play,
                   "CinematicsPreviewToggle",
                   previewRunning ? EditorButtonTone::Danger
                                  : EditorButtonTone::Primary,
                   ImVec2(24.0f, 24.0f),
                   previewRunning ? "Stop preview" : "Play preview")) {
      toggleRequested = true;
    }
    ImGui::SameLine();
    StatusBadge(
        scene.IsRuntimePlayActive()
            ? "Scene Director"
            : (boundCameraObjectId_ ? "Camera Preview" : "Editor Camera"),
        scene.IsRuntimePlayActive() ? EditorStatusTone::Ready
                                    : EditorStatusTone::Normal);
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();

  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  canvasSize.x = (std::max)(canvasSize.x, 1.0f);
  canvasSize.y = (std::max)(canvasSize.y, 1.0f);
  const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
  constexpr float gameViewAspect = 16.0f / 9.0f;
  const EditorViewportFit viewportFit =
      FitEditorViewport(canvasSize.x, canvasSize.y, gameViewAspect);
  const ImVec2 imageSize{viewportFit.width, viewportFit.height};
  const ImVec2 imageOrigin{canvasOrigin.x + viewportFit.offsetX,
                           canvasOrigin.y + viewportFit.offsetY};
  ImGui::GetWindowDrawList()->AddRectFilled(
      canvasOrigin,
      {canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y},
      IM_COL32(4, 6, 9, 255));
  ImGui::SetCursorScreenPos(imageOrigin);
  const bool focused =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  view.extent.width = static_cast<uint32_t>(imageSize.x + 0.5f);
  view.extent.height = static_cast<uint32_t>(imageSize.y + 0.5f);
  view.interaction.focused = focused;
  view.interaction.keyboardActive = focused;
  SERVICES::SetEditorGameViewportSize(static_cast<int>(view.extent.width),
                                      static_cast<int>(view.extent.height),
                                      true);

  const bool ready = POST::PostSystem::IsEditorViewportReady();
  const D3D12_GPU_DESCRIPTOR_HANDLE viewportSrv =
      POST::PostSystem::GetEditorViewportSrv();
  if (ready && viewportSrv.ptr != 0) {
    const ImTextureID textureId =
        reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(viewportSrv.ptr));
    ImGui::Image(textureId, imageSize);
  } else {
    const ImVec2 imageMax{imageOrigin.x + imageSize.x,
                          imageOrigin.y + imageSize.y};
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(imageOrigin, imageMax, IM_COL32(8, 10, 13, 255));
    drawList->AddText(ImVec2(imageOrigin.x + 16.0f, imageOrigin.y + 16.0f),
                      IM_COL32(190, 205, 215, 255),
                      "Waiting for primary render output");
    ImGui::Dummy(imageSize);
  }

  const bool hovered = ImGui::IsItemHovered();
  view.interaction.hovered = hovered;
  view.interaction.mouseCaptured =
      hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
  SetGameViewportInputRect(imageOrigin.x, imageOrigin.y, imageSize.x,
                           imageSize.y, focused);
  ImGui::SetCursorScreenPos({canvasOrigin.x, canvasOrigin.y + canvasSize.y});
  ImGui::Dummy(ImVec2(1.0f, 1.0f));
  ImGui::End();
  ImGui::PopStyleVar();
#else
  (void)scene;
  (void)playSession;
  (void)workspaceHost;
#endif
  return toggleRequested;
}

} // namespace HIKARI::EDITOR
