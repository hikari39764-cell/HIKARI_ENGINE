#include "Editor/Views/Director/HIKARI_DirectorViewPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

#if defined(HIKARI_WITH_EDITOR)
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#endif
#include "Editor/Views/Director/HIKARI_DirectorCameraPose.h"
#include "Editor/Views/Director/HIKARI_DirectorViewInternal.h"
#include "Editor/Views/HIKARI_EditorViewInputGate.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

using namespace DIRECTOR_VIEW;

DirectorViewPanelResult DirectorViewPanel::DrawContents(
    const DocumentSceneBase &scene, EditorViewInstance &view,
    SceneObjectId selectedObjectId, const EditorTransformGizmoState &gizmoState,
    float deltaTime, bool runtimePlayActive) {

  DirectorViewPanelResult result{};
#if defined(HIKARI_WITH_EDITOR)
  bool toolbarCameraChanged = false;
  if (mode_ != DirectorViewMode::Free &&
      (!IsEnabledCamera(scene, targetCameraObjectId_) ||
       (mode_ == DirectorViewMode::Pilot &&
        HasDocumentParent(scene, targetCameraObjectId_)))) {
    SetMode(DirectorViewMode::Free);
  }
  if (runtimePlayActive && mode_ == DirectorViewMode::Pilot) {
    SetMode(DirectorViewMode::Free);
  }

  SceneObjectId toolbarTarget = IsEnabledCamera(scene, selectedObjectId)
                                    ? selectedObjectId
                                    : targetCameraObjectId_;
  const GameObject *selectedObject = FindRuntimeObject(scene, selectedObjectId);
  const bool selectedObjectIsCamera =
      selectedObject != nullptr &&
      selectedObject->GetComponent<CameraComponent>() != nullptr;
  const bool targetEnabled = IsEnabledCamera(scene, toolbarTarget);
  const bool targetHasParent =
      targetEnabled && HasDocumentParent(scene, toolbarTarget);

  const float toolbarHeight = ImGui::GetFrameHeight() * 3.0f +
                              ImGui::GetStyle().ItemSpacing.y * 2.0f + 10.0f;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.045f, 0.052f, 0.064f, 1.0f));
  ImGui::BeginChild("##DirectorToolbar", ImVec2(0.0f, toolbarHeight), false,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);

  if (DrawModeButton("Free", mode_ == DirectorViewMode::Free)) {
    SetMode(DirectorViewMode::Free);
  }
  ImGui::SameLine();
  if (!targetEnabled) {
    ImGui::BeginDisabled();
  }
  if (DrawModeButton("Look Through", mode_ == DirectorViewMode::LookThrough)) {
    SetTargetCamera(toolbarTarget);
    SetMode(DirectorViewMode::LookThrough);
  }
  if (!targetEnabled) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  const bool pilotDisabled =
      !targetEnabled || targetHasParent || runtimePlayActive;
  if (pilotDisabled) {
    ImGui::BeginDisabled();
  }
  if (DrawModeButton("Pilot", mode_ == DirectorViewMode::Pilot)) {
    Camera3D targetCamera{};
    if (scene.TryResolveCameraObjectView(
            toolbarTarget, (std::max)(view.extent.Aspect(), 0.05f),
            targetCamera)) {
      SetTargetCamera(toolbarTarget);
      pilotCamera_.ResetFromCamera(targetCamera);
      SetMode(DirectorViewMode::Pilot);
    }
  }
  if (pilotDisabled) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  const bool alignDisabled =
      !targetEnabled || targetHasParent || runtimePlayActive;
  if (alignDisabled) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Align Camera to View")) {
    result.alignCameraRequested = true;
    result.alignCameraObjectId = toolbarTarget;
    result.alignPose =
        MakeDirectorCameraPose(currentViewCamera_);
  }
  if (alignDisabled) {
    ImGui::EndDisabled();
  }
  if (targetHasParent &&
      ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    ImGui::SetTooltip(
        "Pilot and Align are disabled for parented cameras in this stage.");
  }

  ImGui::SameLine();
  if (mode_ == DirectorViewMode::Pilot) {
    ImGui::TextColored(
        ImVec4(1.0f, 0.58f, 0.20f, 1.0f), "PILOTING %llu",
        static_cast<unsigned long long>(targetCameraObjectId_.value));
  } else {
    ImGui::TextDisabled("%s", ModeLabel(mode_));
  }

  ImGui::Spacing();
  const bool frameDisabled = selectedObject == nullptr || runtimePlayActive;
  if (frameDisabled) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("Frame Selected")) {
    SetMode(DirectorViewMode::Free);
    freeCamera_.Focus(
        ExtractPosition(selectedObject->GetTransform().GetWorldMatrix()));
    toolbarCameraChanged = true;
  }
  if (frameDisabled) {
    ImGui::EndDisabled();
  }

  if (runtimePlayActive) {
    ImGui::BeginDisabled();
  }
  const auto applyPreset =
      [this, &toolbarCameraChanged](EditorDirectorCameraViewPreset preset) {
        SetMode(DirectorViewMode::Free);
        freeCamera_.ApplyViewPreset(preset);
        toolbarCameraChanged = true;
      };
  ImGui::SameLine();
  if (ImGui::Button("Perspective")) {
    applyPreset(EditorDirectorCameraViewPreset::Perspective);
  }
  ImGui::SameLine();
  if (ImGui::Button("Top")) {
    applyPreset(EditorDirectorCameraViewPreset::Top);
  }
  ImGui::SameLine();
  if (ImGui::Button("Front")) {
    applyPreset(EditorDirectorCameraViewPreset::Front);
  }
  ImGui::SameLine();
  if (ImGui::Button("Right")) {
    applyPreset(EditorDirectorCameraViewPreset::Right);
  }
  if (runtimePlayActive) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  ImGui::TextUnformatted("Shading");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(92.0f);
  if (ImGui::BeginCombo("##DirectorShadingMode",
                        ShadingModeLabel(view.visualization.shadingMode))) {
    const EditorViewShadingMode modes[] = {
        EditorViewShadingMode::Lit,
        EditorViewShadingMode::Neutral,
        EditorViewShadingMode::Unlit,
    };
    for (EditorViewShadingMode candidate : modes) {
      const bool selected = candidate == view.visualization.shadingMode;
      if (ImGui::Selectable(ShadingModeLabel(candidate), selected)) {
        view.visualization.shadingMode = candidate;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  ImGui::SameLine();
  ImGui::TextUnformatted("Exposure");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(86.0f);
  ImGui::SliderFloat("##DirectorDisplayExposure",
                     &view.visualization.displayExposure, 0.25f, 4.0f, "%.2fx");

  ImGui::SameLine();
  ImGui::TextUnformatted("Cameras");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(82.0f);
  if (ImGui::BeginCombo("##DirectorCameraOverlays",
                        CameraOverlayModeLabel(view.visualization))) {
    if (ImGui::Selectable("All",
                          view.visualization.showCameraOverlays &&
                              !view.visualization.showOnlySelectedCamera)) {
      view.visualization.showCameraOverlays = true;
      view.visualization.showOnlySelectedCamera = false;
    }
    if (ImGui::Selectable("Selected",
                          view.visualization.showCameraOverlays &&
                              view.visualization.showOnlySelectedCamera)) {
      view.visualization.showCameraOverlays = true;
      view.visualization.showOnlySelectedCamera = true;
    }
    if (ImGui::Selectable("Off", !view.visualization.showCameraOverlays)) {
      view.visualization.showCameraOverlays = false;
      view.visualization.showOnlySelectedCamera = false;
    }
    ImGui::EndCombo();
  }

  if (view.visualization.showCameraOverlays) {
    ImGui::SameLine();
    ImGui::TextUnformatted("Size");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(78.0f);
    ImGui::SliderFloat("##DirectorCameraOverlayScale",
                       &view.visualization.cameraOverlayScale, 0.5f, 2.0f,
                       "%.1fx");
  }

  ImGui::Spacing();
  if (selectedObjectIsCamera &&
      gizmoState.operation == EditorTransformGizmoOperation::Scale) {
    ImGui::TextColored(
        ImVec4(1.0f, 0.66f, 0.30f, 1.0f),
        "Camera scale is not a lens control; edit FOV in Camera settings.");
  } else if (mode_ == DirectorViewMode::Pilot) {
    ImGui::TextColored(
        ImVec4(1.0f, 0.66f, 0.30f, 1.0f),
        "Hold RMB: look + WASDQE move | Shift: boost | Esc: exit Pilot");
  } else {
    ImGui::TextDisabled("Hold RMB: look + WASDQE move | Shift: boost | "
                        "Alt+LMB: orbit | MMB: pan | F: focus");
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();

  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  canvasSize.x = (std::max)(canvasSize.x, kMinimumCanvasSize);
  canvasSize.y = (std::max)(canvasSize.y, kMinimumCanvasSize);
  const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
  const ImVec2 imageSize = canvasSize;
  const ImVec2 imageOrigin = canvasOrigin;
  ImGui::GetWindowDrawList()->AddRectFilled(
      canvasOrigin,
      {canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y},
      IM_COL32(12, 16, 22, 255));
  ImGui::SetCursorScreenPos(imageOrigin);
  view.extent.width = ExtentDimension(imageSize.x);
  view.extent.height = ExtentDimension(imageSize.y);
  const float aspect = (std::max)(view.extent.Aspect(), 0.05f);

  const RENDER3D::EDITORVIEW::EditorInteractiveViewOutput output =
      RENDER3D::EDITORVIEW::GetOutput(view.renderViewId);
  const bool outputReady =
      output.ready && output.colorSrv.ptr != 0 && output.width > 0 &&
      output.height > 0 &&
      output.sceneRevision == scene.GetSceneDocumentRevision();
  if (outputReady) {
    const ImTextureID texture = reinterpret_cast<ImTextureID>(
        static_cast<uintptr_t>(output.colorSrv.ptr));
    ImGui::Image(texture, imageSize);
  } else {
    ImGui::InvisibleButton("##DirectorViewportCanvas", imageSize,
                           ImGuiButtonFlags_MouseButtonLeft |
                               ImGuiButtonFlags_MouseButtonRight |
                               ImGuiButtonFlags_MouseButtonMiddle);
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        imageOrigin, {imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y},
        IM_COL32(9, 12, 16, 255));
    drawList->AddText({imageOrigin.x + 14.0f, imageOrigin.y + 14.0f},
                      IM_COL32(184, 198, 211, 255),
                      "Waiting for Director View output");
  }

  const bool imageHovered = ImGui::IsItemHovered();
  const bool windowFocused =
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  const ImGuiIO &io = ImGui::GetIO();
  const EditorViewInputBlockState inputBlock = QueryEditorViewInputBlockState();
  EditorViewInputSubmission inputSubmission{};
  inputSubmission.rect = {imageOrigin.x, imageOrigin.y, imageSize.x,
                          imageSize.y};
  inputSubmission.visible = ImGui::IsItemVisible();
  inputSubmission.focused = windowFocused;
  inputSubmission.hovered = imageHovered;
  inputSubmission.rightMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
  inputSubmission.rightMouseClicked =
      ImGui::IsMouseClicked(ImGuiMouseButton_Right);
  inputSubmission.middleMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
  inputSubmission.middleMouseClicked =
      ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
  inputSubmission.leftMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
  inputSubmission.leftMouseClicked =
      ImGui::IsMouseClicked(ImGuiMouseButton_Left);
  inputSubmission.altDown = io.KeyAlt;
  inputSubmission.pointerBlocked = inputBlock.pointer;
  inputSubmission.keyboardBlocked = inputBlock.keyboard;
  // Gizmo capture belongs to the current frame. Clear the previous-frame
  // value before arbitrating a new RMB/MMB/Alt+LMB press.
  inputRouter_.SetGizmoCapture(view.renderViewId, false);
  const EditorViewInputState &inputState =
      inputRouter_.Submit(view.renderViewId, inputSubmission);

  const bool navigationPressed =
      !inputBlock.pointer && imageHovered &&
      (inputSubmission.rightMouseClicked ||
       inputSubmission.middleMouseClicked ||
       (inputSubmission.leftMouseClicked && inputSubmission.altDown));
  if (navigationPressed) {
    ImGui::SetWindowFocus();
    ImGui::ClearActiveID();
  }

  view.interaction.visible = inputState.visible;
  view.interaction.focused = inputState.focused;
  view.interaction.hovered = inputState.hovered;
  view.interaction.mouseCaptured = inputState.IsMouseCaptured();
  view.interaction.keyboardActive = inputState.AcceptsKeyboard();

  if (mode_ == DirectorViewMode::Pilot && !inputBlock.keyboard &&
      ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
    SetMode(DirectorViewMode::Free);
  }

  EditorDirectorCameraInput cameraInput{};
  const bool allowNavigation = mode_ != DirectorViewMode::LookThrough &&
                               !runtimePlayActive &&
                               !inputState.gizmoCaptured && !inputBlock.pointer;
  if (allowNavigation) {
    cameraInput.lookActive = inputState.rightMouseCaptured;
    cameraInput.orbitActive = inputState.orbitMouseCaptured;
    cameraInput.panActive = inputState.middleMouseCaptured;
    cameraInput.mouseDeltaX = io.MouseDelta.x;
    cameraInput.mouseDeltaY = io.MouseDelta.y;
    cameraInput.wheelDelta = inputState.AcceptsWheel() ? io.MouseWheel : 0.0f;
    cameraInput.fast = io.KeyShift;
    if (inputState.rightMouseCaptured) {
      cameraInput.moveForward = ImGui::IsKeyDown(ImGuiKey_W);
      cameraInput.moveBackward = ImGui::IsKeyDown(ImGuiKey_S);
      cameraInput.moveRight = ImGui::IsKeyDown(ImGuiKey_D);
      cameraInput.moveLeft = ImGui::IsKeyDown(ImGuiKey_A);
      cameraInput.moveUp = ImGui::IsKeyDown(ImGuiKey_E);
      cameraInput.moveDown = ImGui::IsKeyDown(ImGuiKey_Q);
    }
  }

  bool cameraChanged = toolbarCameraChanged;
  if (allowNavigation && inputState.AcceptsKeyboard() && imageHovered &&
      !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
    if (const GameObject *selectedObject =
            FindRuntimeObject(scene, selectedObjectId)) {
      ActiveController().Focus(
          ExtractPosition(selectedObject->GetTransform().GetWorldMatrix()));
      cameraChanged = true;
    }
  }

  if (allowNavigation) {
    cameraChanged = ActiveController().Update(cameraInput, deltaTime, aspect) ||
                    cameraChanged;
  }

  if (mode_ == DirectorViewMode::LookThrough) {
    if (!scene.TryResolveCameraObjectView(targetCameraObjectId_, aspect,
                                          resolvedLookCamera_)) {
      SetMode(DirectorViewMode::Free);
      currentViewCamera_ = freeCamera_.GetCamera();
    } else {
      currentViewCamera_ = resolvedLookCamera_;
    }
  } else {
    currentViewCamera_ = ActiveController().GetCamera();
  }

  if (mode_ == DirectorViewMode::Pilot && cameraChanged) {
    result.pilotPoseChanged = true;
    result.pilotCameraObjectId = targetCameraObjectId_;
    result.pilotPose = pilotCamera_.GetPose();
  }

  if (mode_ == DirectorViewMode::Free) {
    view.cameraBinding.kind = EditorViewCameraSourceKind::OwnedEditorCamera;
    view.cameraBinding.sceneObjectId = {};
  } else {
    view.cameraBinding.kind = EditorViewCameraSourceKind::SceneCameraObject;
    view.cameraBinding.sceneObjectId = targetCameraObjectId_;
  }

  const bool projectionChanged = std::abs(lastAspect_ - aspect) > 1.0e-5f;
  if (cameraChanged || projectionChanged || cameraCutPending_) {
    ++cameraRevision_;
  }
  lastAspect_ = aspect;

  const bool gizmoAllowed = !runtimePlayActive && !inputBlock.pointer &&
                            mode_ != DirectorViewMode::Pilot;
  if (gizmoAllowed &&
      !(selectedObjectIsCamera &&
        gizmoState.operation == EditorTransformGizmoOperation::Scale)) {
    if (selectedObject != nullptr) {
      GameObject proxy{"Director Gizmo Proxy"};
      proxy.SetDocumentId(selectedObjectId);
      Transform3D proxyTransform = selectedObject->GetTransform();
      if (selectedObjectIsCamera) {
        proxyTransform.scale = {1.0f, 1.0f, 1.0f};
        proxyTransform.useExplicitMatrix = false;
      }
      (void)proxy.SetLocalTransform(proxyTransform);
      EditorTransformGizmoState directorGizmoState = gizmoState;
      if (io.KeyCtrl) {
        directorGizmoState.snapEnabled = true;
      }
      const EditorTransformGizmoResult gizmoResult = transformGizmo_.Draw(
          proxy, currentViewCamera_, directorGizmoState,
          {imageOrigin.x, imageOrigin.y, imageSize.x, imageSize.y});
      result.gizmo.interacting = gizmoResult.interacting;
      result.gizmo.changed = gizmoResult.changed;
      result.gizmo.objectId = selectedObjectId;
      result.gizmo.transform = gizmoResult.transform;
      result.gizmo.rotation = gizmoResult.rotation;
    }
  }
  inputRouter_.SetGizmoCapture(view.renderViewId, result.gizmo.interacting);
  view.interaction.gizmoCaptured = result.gizmo.interacting;

  const bool cameraSelectionClicked =
      !inputBlock.pointer && imageHovered &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt &&
      !result.gizmo.interacting;
  const SceneObjectId clickedCamera = DrawCameraOverlays(
      scene, selectedObjectId, currentViewCamera_, imageOrigin, imageSize,
      cameraSelectionClicked, view.visualization);
  if (cameraSelectionClicked && clickedCamera.value != 0) {
    result.selectionRequested = true;
    result.selectedObjectId = clickedCamera;
    SetTargetCamera(clickedCamera);
  }

  RENDER3D::EDITORVIEW::EditorInteractiveViewRequest request{};
  request.viewId = view.renderViewId;
  request.cameraFrame.camera = currentViewCamera_;
  request.cameraFrame.sourceCameraObjectId =
      mode_ == DirectorViewMode::Free ? 0 : targetCameraObjectId_.value;
  request.cameraFrame.revision = cameraRevision_;
  request.cameraFrame.cameraCut = cameraCutPending_;
  request.cameraFrame.projectionChanged = projectionChanged;
  request.cameraFrame.valid = true;
  request.width = view.extent.width;
  request.height = view.extent.height;
  request.sceneRevision = scene.GetSceneDocumentRevision();
  request.visible = view.interaction.visible;
  request.shadingMode = ResolveShadingMode(view.visualization.shadingMode);
  request.displayExposure = view.visualization.displayExposure;
  // Camera overlays and the transform gizmo are drawn explicitly by this
  // panel. Replaying every Scene debug helper here creates duplicate light
  // markers and oversized frusta that obscure camera editing.
  request.drawDebug = false;
  RENDER3D::EDITORVIEW::SubmitRequest(request);
  cameraCutPending_ = false;

  ImGui::SetCursorScreenPos({canvasOrigin.x, canvasOrigin.y + canvasSize.y});
  ImGui::Dummy(ImVec2(1.0f, 1.0f));

  result.mode = mode_;
  result.targetCameraObjectId = targetCameraObjectId_;
#else
  (void)scene;
  (void)view;
  (void)selectedObjectId;
  (void)gizmoState;
  (void)deltaTime;
  (void)runtimePlayActive;
#endif
  return result;
}

} // namespace HIKARI::EDITOR
