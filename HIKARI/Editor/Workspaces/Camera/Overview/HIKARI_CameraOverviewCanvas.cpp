#include "Editor/Workspaces/Camera/Overview/HIKARI_CameraOverviewPanel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Views/HIKARI_EditorViewInputGate.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
#include "Editor/Workspaces/Camera/Overview/HIKARI_CameraOverviewInternal.h"

namespace HIKARI::EDITOR {

using namespace CAMERA_OVERVIEW;

CameraOverviewPanelResult
CameraOverviewPanel::DrawOverviewContents(DocumentSceneBase &scene,
                                          EditorContext &context,
                                          EditorViewInstance &view) const {

#if defined(HIKARI_WITH_EDITOR)
  CameraOverviewPanelResult result{};
  std::vector<OverviewCameraEntry> cameras = GatherCameras(scene, context);

  if (IconTextButton(EditorGlyph::Focus, "Fit All", "CameraOverviewFitAll",
                     EditorButtonTone::Neutral, ImVec2(0.0f, 28.0f),
                     "Fit all cameras in the overview")) {
    FitCameras(view, cameras);
  }
  const OverviewCameraEntry *selectedCamera = FindSelectedCamera(cameras);
  ImGui::SameLine();
  if (selectedCamera == nullptr) {
    ImGui::BeginDisabled();
  }
  if (IconButton(EditorGlyph::Focus, "CameraOverviewFocusSelected",
                 EditorButtonTone::Quiet, ImVec2(28.0f, 28.0f),
                 "Focus selected camera")) {
    view.overviewCenterXZ = selectedCamera->positionXZ;
  }
  if (selectedCamera == nullptr) {
    ImGui::EndDisabled();
  }
  ImGui::SameLine();
  if (IconToggleButton(EditorGlyph::Object, "CameraOverviewObjectMarkers",
                       view.visualization.showSceneObjectMarkers,
                       ImVec2(28.0f, 28.0f),
                       view.visualization.showSceneObjectMarkers
                           ? "Hide scene object markers"
                           : "Show scene object markers")) {
    view.visualization.showSceneObjectMarkers =
        !view.visualization.showSceneObjectMarkers;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(82.0f);
  ImGui::SliderFloat("##OverviewCameraMarkerScale",
                     &view.visualization.cameraOverlayScale, 0.5f, 2.0f,
                     "%.1fx");
  ImGui::SameLine();
  StatusBadge((std::to_string(cameras.size()) + " cameras").c_str(),
              EditorStatusTone::Normal);
  ImGui::SameLine();
  if (IconButton(EditorGlyph::More, "CameraOverviewControls",
                 EditorButtonTone::Quiet, ImVec2(28.0f, 28.0f),
                 "Overview controls")) {
    ImGui::OpenPopup("CameraOverviewControlsPopup");
  }
  if (ImGui::BeginPopup("CameraOverviewControlsPopup")) {
    ImGui::TextDisabled("Middle-drag   Pan");
    ImGui::TextDisabled("Mouse wheel  Zoom");
    ImGui::TextDisabled("Left click   Select camera");
    ImGui::EndPopup();
  }

  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  canvasSize.x = (std::max)(canvasSize.x, kMinCanvasSize);
  canvasSize.y = (std::max)(canvasSize.y, kMinCanvasSize);
  const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##CameraOverviewCanvas", canvasSize,
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonMiddle);
  const ImVec2 canvasMax = ImGui::GetItemRectMax();
  const ImVec2 canvasCenter{(canvasMin.x + canvasMax.x) * 0.5f,
                            (canvasMin.y + canvasMax.y) * 0.5f};

  view.extent.width = ToExtentDimension(canvasSize.x);
  view.extent.height = ToExtentDimension(canvasSize.y);
  view.interaction.visible = ImGui::IsItemVisible();
  view.interaction.focused =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  const EditorViewInputBlockState inputBlock = QueryEditorViewInputBlockState();
  view.interaction.hovered = !inputBlock.pointer && ImGui::IsItemHovered();
  view.interaction.gizmoCaptured = false;
  view.interaction.keyboardActive = false;
  view.overviewUnitsPerScreen = std::clamp(
      view.overviewUnitsPerScreen, kMinOverviewUnits, kMaxOverviewUnits);

  float pixelsPerUnit = canvasSize.x / view.overviewUnitsPerScreen;
  if (view.interaction.hovered &&
      ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    view.overviewCenterXZ.x -= delta.x / pixelsPerUnit;
    view.overviewCenterXZ.y += delta.y / pixelsPerUnit;
  }

  const float mouseWheel =
      view.interaction.hovered ? ImGui::GetIO().MouseWheel : 0.0f;
  if (mouseWheel != 0.0f) {
    const ImVec2 mousePosition = ImGui::GetMousePos();
    const MATH::Vec2 cursorWorld = ScreenToWorld(
        mousePosition, view.overviewCenterXZ, canvasCenter, pixelsPerUnit);
    view.overviewUnitsPerScreen =
        std::clamp(view.overviewUnitsPerScreen * std::pow(0.85f, mouseWheel),
                   kMinOverviewUnits, kMaxOverviewUnits);
    pixelsPerUnit = canvasSize.x / view.overviewUnitsPerScreen;
    view.overviewCenterXZ.x =
        cursorWorld.x - (mousePosition.x - canvasCenter.x) / pixelsPerUnit;
    view.overviewCenterXZ.y =
        cursorWorld.y + (mousePosition.y - canvasCenter.y) / pixelsPerUnit;
  }

  const bool cameraSelectionClicked =
      view.interaction.hovered && ImGui::IsItemClicked(ImGuiMouseButton_Left);
  view.interaction.mouseCaptured =
      view.interaction.hovered && (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                                   ImGui::IsMouseDown(ImGuiMouseButton_Middle));

  if (cameraSelectionClicked) {
    const ImVec2 mousePosition = ImGui::GetMousePos();
    const OverviewCameraEntry *nearestCamera = nullptr;
    const float hitRadius =
        kCameraHitRadius *
        std::clamp(view.visualization.cameraOverlayScale, 0.5f, 2.0f);
    float nearestDistanceSquared = hitRadius * hitRadius;
    for (const OverviewCameraEntry &camera : cameras) {
      const ImVec2 screen =
          WorldToScreen(camera.positionXZ, view.overviewCenterXZ, canvasCenter,
                        pixelsPerUnit);
      const float dx = screen.x - mousePosition.x;
      const float dy = screen.y - mousePosition.y;
      const float distanceSquared = dx * dx + dy * dy;
      if (distanceSquared <= nearestDistanceSquared) {
        nearestDistanceSquared = distanceSquared;
        nearestCamera = &camera;
      }
    }
    if (nearestCamera) {
      SelectRuntimeObject(context,
                          FindRuntimeObject(scene, nearestCamera->objectId));
      result =
          MakeAction(CameraOverviewActionKind::Select, nearestCamera->objectId);
      cameras = GatherCameras(scene, context);
    }
  }

  ImDrawList *drawList = ImGui::GetWindowDrawList();
  drawList->PushClipRect(canvasMin, canvasMax, true);
  drawList->AddRectFilled(canvasMin, canvasMax, IM_COL32(32, 41, 52, 255));
  DrawGrid(*drawList, canvasMin, canvasMax, canvasCenter, view, pixelsPerUnit);
  if (view.visualization.showSceneObjectMarkers) {
    DrawSceneObjectPoints(*drawList, scene, view, canvasCenter, pixelsPerUnit);
  }
  for (const OverviewCameraEntry &camera : cameras) {
    DrawCameraMarker(*drawList, camera, view, canvasCenter, pixelsPerUnit);
  }
  drawList->AddRect(canvasMin, canvasMax, IM_COL32(128, 151, 174, 240), 3.0f, 0,
                    1.0f);
  drawList->PopClipRect();
  return result;
#else
  (void)scene;
  (void)context;
  (void)view;
  return {};
#endif
}

} // namespace HIKARI::EDITOR
