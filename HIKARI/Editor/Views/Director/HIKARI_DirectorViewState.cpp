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
#include "Editor/Views/HIKARI_EditorViewInputGate.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

DirectorViewPanel::DirectorViewPanel() {
  currentViewCamera_ = freeCamera_.GetCamera();
}

void DirectorViewPanel::SetTargetCamera(SceneObjectId cameraObjectId) noexcept {
  if (cameraObjectId == targetCameraObjectId_) {
    return;
  }
  if (mode_ == DirectorViewMode::Pilot) {
    SetMode(DirectorViewMode::Free);
  }
  targetCameraObjectId_ = cameraObjectId;
}

void DirectorViewPanel::ExitPilot() noexcept {
  if (mode_ == DirectorViewMode::Pilot) {
    SetMode(DirectorViewMode::Free);
  }
}

void DirectorViewPanel::ResetForScene() {
#if defined(HIKARI_WITH_EDITOR)
  RENDER3D::EDITORVIEW::ClearRequest(kEditorDirectorRenderViewId);
#endif
  mode_ = DirectorViewMode::Free;
  targetCameraObjectId_ = {};
  freeCamera_.Reset();
  pilotCamera_.Reset();
  currentViewCamera_ = freeCamera_.GetCamera();
  inputRouter_.Clear(kEditorDirectorRenderViewId);
  cameraRevision_ = 1;
  lastAspect_ = 0.0f;
  cameraCutPending_ = true;
}

void DirectorViewPanel::LeaveWorkspace() {
#if defined(HIKARI_WITH_EDITOR)
  RENDER3D::EDITORVIEW::ClearRequest(kEditorDirectorRenderViewId);
#endif
  SetMode(DirectorViewMode::Free);
  targetCameraObjectId_ = {};
  inputRouter_.Clear(kEditorDirectorRenderViewId);
}

DirectorViewMode DirectorViewPanel::GetMode() const noexcept { return mode_; }

SceneObjectId DirectorViewPanel::GetTargetCameraObjectId() const noexcept {
  return targetCameraObjectId_;
}

const Camera3D &DirectorViewPanel::GetViewCamera() const noexcept {
  return currentViewCamera_;
}

EditorDirectorCameraController &DirectorViewPanel::ActiveController() noexcept {
  return mode_ == DirectorViewMode::Pilot ? pilotCamera_ : freeCamera_;
}

const EditorDirectorCameraController &
DirectorViewPanel::ActiveController() const noexcept {
  return mode_ == DirectorViewMode::Pilot ? pilotCamera_ : freeCamera_;
}

void DirectorViewPanel::SetMode(DirectorViewMode mode) noexcept {
  if (mode_ == mode) {
    return;
  }
  mode_ = mode;
  cameraCutPending_ = true;
}

} // namespace HIKARI::EDITOR
