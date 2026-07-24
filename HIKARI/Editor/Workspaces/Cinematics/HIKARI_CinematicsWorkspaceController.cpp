#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"

#include <algorithm>
#include <cmath>

#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/Views/Director/HIKARI_DirectorCameraPose.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

namespace {
bool IsEnabledCameraObject(const DocumentSceneBase &scene,
                           SceneObjectId cameraObjectId) {

  if (cameraObjectId.value == 0) {
    return false;
  }
  const GameObject *object = scene.GetWorld().FindObject(cameraObjectId);
  const CameraComponent *camera =
      object != nullptr ? object->GetComponent<CameraComponent>() : nullptr;
  return camera != nullptr && camera->IsEnabled();
}

GameObject *FindRuntimeObject(DocumentSceneBase &scene,
                              SceneObjectId objectId) {

  return scene.GetWorld().FindObject(objectId);
}

CameraBlendDesc
CameraBlendFromTimelineResult(const CameraTimelinePanelResult &result) {

  CameraBlendDesc blend{};
  if (result.forceCameraCut || result.evaluation.transition.mode ==
                                   SEQUENCER::CameraCutTransitionMode::Cut) {
    return blend;
  }
  blend.mode = CameraBlendMode::EaseInOut;
  blend.durationSeconds = result.evaluation.transition.durationSeconds;
  return blend;
}
} // namespace

bool CinematicsWorkspaceController::RequestOpenSequenceAsset(
    DocumentSceneBase &scene, const AssetGuid &assetGuid,
    std::string &outMessage) {

  const bool opened = sequenceDocumentController_.RequestOpenAsset(
      scene.GetAssetDatabase(), assetGuid, outMessage);
  sequenceLibraryPanel_.Select(assetGuid);
  if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
    cameraTimelinePanel_.ResetForScene();
  }
  return opened;
}

bool CinematicsWorkspaceController::IsEditingSequenceAsset() const noexcept {

  return sequenceDocumentController_.GetDocument().IsExternalDocument();
}

bool CinematicsWorkspaceController::IsSequenceDocumentDirty() const noexcept {

  return sequenceDocumentController_.GetDocument().IsDirty();
}

bool CinematicsWorkspaceController::CanUndoSequenceDocument() const noexcept {

  return sequenceDocumentController_.GetDocument().CanUndo();
}

bool CinematicsWorkspaceController::CanRedoSequenceDocument() const noexcept {

  return sequenceDocumentController_.GetDocument().CanRedo();
}

bool CinematicsWorkspaceController::SaveSequenceDocument(
    DocumentSceneBase &scene, std::string &outMessage) {

  const bool saved =
      sequenceDocumentController_.Save(scene.GetAssetDatabase(), outMessage);
  if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
    cameraTimelinePanel_.ResetForScene();
  }
  return saved;
}

bool CinematicsWorkspaceController::UndoSequenceDocument(
    std::string &outMessage) {

  const bool changed = sequenceDocumentController_.Undo(outMessage);
  if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
    cameraTimelinePanel_.ResetForScene();
  }
  return changed;
}

bool CinematicsWorkspaceController::RedoSequenceDocument(
    std::string &outMessage) {

  const bool changed = sequenceDocumentController_.Redo(outMessage);
  if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
    cameraTimelinePanel_.ResetForScene();
  }
  return changed;
}

void CinematicsWorkspaceController::BindDocumentCommands(
    EditorCommandRouter &commandRouter, DocumentSceneBase &scene) {

  commandRouter.Bind(EditorCommandId::SaveDocument, "Save Sequence Asset",
                     IsEditingSequenceAsset(), [this, &scene]() {
                       (void)SaveSequenceDocument(scene, pendingStatusMessage_);
                     });
  commandRouter.Bind(
      EditorCommandId::Undo, "Undo Sequence Edit", CanUndoSequenceDocument(),
      [this]() { (void)UndoSequenceDocument(pendingStatusMessage_); });
  commandRouter.Bind(
      EditorCommandId::Redo, "Redo Sequence Edit", CanRedoSequenceDocument(),
      [this]() { (void)RedoSequenceDocument(pendingStatusMessage_); });
}

void CinematicsWorkspaceController::PrepareForRuntimePlay() {
  directorViewPanel_.ExitPilot();
}

void CinematicsWorkspaceController::OnCinematicsDocumentRestored(
    DocumentSceneBase &scene, EditorWorkspaceHost &workspaceHost) {

  cameraTimelinePanel_.ResetForScene();
  RestoreTimelinePreview(scene, workspaceHost);
  timelinePreviewOwned_ = false;
  timelinePreviewShotId_ = 0;
  timelineRestoreCameraObjectId_.reset();
}

void CinematicsWorkspaceController::ApplyWorkspaceActivation(
    DocumentSceneBase &scene, const EditorWorkspaceActivation &activation,
    EditorContext &context, EditorWorkspaceHost &workspaceHost) {

  if (activation.current == EditorWorkspaceId::Cinematics &&
      activation.sequenceAssetGuid && activation.sequenceAssetGuid->IsValid()) {
    (void)RequestOpenSequenceAsset(scene, *activation.sequenceAssetGuid,
                                   pendingStatusMessage_);
  }

  if (activation.previous == EditorWorkspaceId::Cinematics &&
      activation.current != EditorWorkspaceId::Cinematics) {
    cameraTimelinePanel_.PausePlayback();
    timelinePreviewOwned_ = false;
    timelinePreviewShotId_ = 0;
    timelineRestoreCameraObjectId_.reset();
    if (cameraPreviewOwned_) {
      scene.EndEditorCameraPreview();
      if (preWorkspacePreviewObjectId_ && !scene.IsRuntimePlayActive()) {
        (void)scene.BeginEditorCameraPreview(*preWorkspacePreviewObjectId_);
      }
    }
    cameraPreviewOwned_ = false;
    preWorkspacePreviewObjectId_.reset();
    ClearCameraBinding(workspaceHost);
    directorViewPanel_.LeaveWorkspace();
    return;
  }

  if (activation.current != EditorWorkspaceId::Cinematics) {
    return;
  }
  if (activation.previous == EditorWorkspaceId::Cinematics &&
      !activation.targetCameraObjectId && !activation.sequenceAssetGuid) {
    return;
  }

  if (activation.previous != EditorWorkspaceId::Cinematics) {
    cameraPreviewOwned_ = false;
    preWorkspacePreviewObjectId_.reset();
    if (scene.IsEditorCameraPreviewActive()) {
      preWorkspacePreviewObjectId_ = scene.GetEditorCameraPreviewObjectId();
    }
  }

  std::optional<SceneObjectId> targetCamera = activation.targetCameraObjectId;
  if (!targetCamera && activation.previous != EditorWorkspaceId::Cinematics) {
    targetCamera = preWorkspacePreviewObjectId_
                       ? preWorkspacePreviewObjectId_
                       : scene.GetSceneDocument().camera.defaultCameraObjectId;
  }
  if (!targetCamera || !IsEnabledCameraObject(scene, *targetCamera)) {
    std::optional<SceneObjectId> activePreviewCamera;
    if (scene.IsEditorCameraPreviewActive()) {
      activePreviewCamera = scene.GetEditorCameraPreviewObjectId();
    }
    if (activePreviewCamera &&
        IsEnabledCameraObject(scene, *activePreviewCamera)) {
      targetCamera = activePreviewCamera;
    } else {
      if (cameraPreviewOwned_ && !scene.IsRuntimePlayActive()) {
        scene.EndEditorCameraPreview();
      }
      cameraPreviewOwned_ = false;
      ClearCameraBinding(workspaceHost);
      return;
    }
  }

  boundCameraObjectId_ = *targetCamera;
  directorViewPanel_.SetTargetCamera(*targetCamera);
  EditorViewInstance &gameView = workspaceHost.GetCinematicsGameView();
  gameView.cameraBinding.kind = EditorViewCameraSourceKind::SceneCameraObject;
  gameView.cameraBinding.sceneObjectId = *targetCamera;

  if (!scene.IsRuntimePlayActive() &&
      (!scene.IsEditorCameraPreviewActive() ||
       !(scene.GetEditorCameraPreviewObjectId() == *targetCamera))) {
    cameraPreviewOwned_ = scene.BeginEditorCameraPreview(*targetCamera);
    if (!cameraPreviewOwned_) {
      ClearCameraBinding(workspaceHost);
    }
  }
}

void CinematicsWorkspaceController::SyncSceneIdentity(
    DocumentSceneBase &scene, EditorWorkspaceHost &workspaceHost) {

  const uint64_t currentRevision = scene.GetSceneDocumentRevision();
  const AssetGuid &sceneGuid = scene.GetCurrentSceneAssetGuid();
  const std::string currentIdentity =
      sceneGuid.IsValid() ? "Asset:" + sceneGuid.value
                          : "Transient:" + scene.GetSceneId() + ":" +
                                std::to_string(currentRevision);
  if (sceneIdentity_.empty()) {
    sceneIdentity_ = currentIdentity;
    sceneDocumentRevision_ = currentRevision;
    return;
  }

  const bool sceneChanged = sceneIdentity_ != currentIdentity;
  const bool documentReloaded = sceneDocumentRevision_ != currentRevision;
  if (!sceneChanged && !documentReloaded) {
    return;
  }

  directorViewPanel_.ResetForScene();
  cameraTimelinePanel_.ResetForScene();
  cameraPreviewOwned_ = false;
  timelinePreviewOwned_ = false;
  timelinePreviewShotId_ = 0;
  preWorkspacePreviewObjectId_.reset();
  timelineRestoreCameraObjectId_.reset();
  if (sceneChanged || (boundCameraObjectId_ &&
                       !IsEnabledCameraObject(scene, *boundCameraObjectId_))) {
    ClearCameraBinding(workspaceHost);
  }

  if (workspaceHost.IsActive(EditorWorkspaceId::Cinematics) &&
      !boundCameraObjectId_) {
    const std::optional<SceneObjectId> defaultCamera =
        scene.GetSceneDocument().camera.defaultCameraObjectId;
    if (defaultCamera && IsEnabledCameraObject(scene, *defaultCamera)) {
      boundCameraObjectId_ = *defaultCamera;
      directorViewPanel_.SetTargetCamera(*defaultCamera);
      EditorViewInstance &gameView = workspaceHost.GetCinematicsGameView();
      gameView.cameraBinding.kind =
          EditorViewCameraSourceKind::SceneCameraObject;
      gameView.cameraBinding.sceneObjectId = *defaultCamera;
    }
  }

  sceneIdentity_ = currentIdentity;
  sceneDocumentRevision_ = currentRevision;
}

void CinematicsWorkspaceController::ApplyCameraOverviewAction(
    DocumentSceneBase &scene, const CameraOverviewAction &action,
    EditorContext &context, EditorWorkspaceHost &workspaceHost,
    CinematicsWorkspaceResult &result) {

  if (!action.IsValid()) {
    return;
  }

  switch (action.kind) {
  case CameraOverviewActionKind::Select:
    directorViewPanel_.SetTargetCamera(action.cameraObjectId);
    break;
  case CameraOverviewActionKind::SetDefault:
    directorViewPanel_.SetTargetCamera(action.cameraObjectId);
    if (scene.SetGameDefaultCamera(action.cameraObjectId)) {
      context.sceneDirty = true;
      scene.SetUnsavedSceneChanges(true);
    }
    break;
  case CameraOverviewActionKind::ViewThrough:
    cameraTimelinePanel_.SuspendPreview();
    timelinePreviewOwned_ = false;
    timelinePreviewShotId_ = 0;
    timelineRestoreCameraObjectId_.reset();
    directorViewPanel_.SetTargetCamera(action.cameraObjectId);
    if (scene.IsRuntimePlayActive()) {
      result.statusMessage =
          "Camera preview is unavailable while Play is running";
      break;
    }
    if (BindCameraPreview(scene, action.cameraObjectId, workspaceHost)) {
      cameraPreviewOwned_ = true;
    }
    break;
  case CameraOverviewActionKind::ExitView:
    cameraTimelinePanel_.SuspendPreview();
    timelinePreviewOwned_ = false;
    timelinePreviewShotId_ = 0;
    timelineRestoreCameraObjectId_.reset();
    scene.EndEditorCameraPreview();
    cameraPreviewOwned_ = false;
    preWorkspacePreviewObjectId_.reset();
    ClearCameraBinding(workspaceHost);
    break;
  case CameraOverviewActionKind::SnapToView: {
    const DirectorCameraPose pose =
        MakeDirectorCameraPose(
            directorViewPanel_.GetViewCamera());
    if (scene.ApplyCameraObjectPose(action.cameraObjectId, pose.position,
                                    pose.rotation, true)) {
      directorViewPanel_.SetTargetCamera(action.cameraObjectId);
      context.sceneDirty = true;
      scene.SetUnsavedSceneChanges(true);
    }
  } break;
  default:
    break;
  }
}

void CinematicsWorkspaceController::ApplyDirectorViewResult(
    DocumentSceneBase &scene, const DirectorViewPanelResult &result,
    EditorContext &context, SelectionSyncService &selectionSync) {

  if (result.selectionRequested) {
    if (GameObject *object =
            FindRuntimeObject(scene, result.selectedObjectId)) {
      context.selection.SelectObject(scene.GetWorld(), object);
    }
  }

  const auto applyCameraPose = [&scene,
                                &context](SceneObjectId cameraObjectId,
                                          const DirectorCameraPose &pose) {
    if (!scene.ApplyCameraObjectPose(cameraObjectId, pose.position,
                                     pose.rotation, true)) {
      return false;
    }
    context.sceneDirty = true;
    scene.SetUnsavedSceneChanges(true);
    return true;
  };

  if (result.alignCameraRequested) {
    (void)applyCameraPose(result.alignCameraObjectId, result.alignPose);
  }
  if (result.pilotPoseChanged) {
    (void)applyCameraPose(result.pilotCameraObjectId, result.pilotPose);
  }

  if (result.gizmo.changed) {
    if (GameObject *object = FindRuntimeObject(scene, result.gizmo.objectId)) {
      const bool cameraPoseApplied =
          object->GetComponent<CameraComponent>() != nullptr &&
          scene.ApplyCameraObjectPose(result.gizmo.objectId,
                                      result.gizmo.transform.position,
                                      result.gizmo.rotation, true);
      if (!cameraPoseApplied) {
        Transform3D runtimeTransform = object->GetTransform();
        runtimeTransform.position = result.gizmo.transform.position;
        runtimeTransform.rotation = MATH::NormalizeQ(result.gizmo.rotation);
        runtimeTransform.scale = result.gizmo.transform.scale;
        runtimeTransform.useExplicitMatrix = false;
        (void)object->SetLocalTransform(runtimeTransform);
      }

      if (SceneObjectData *documentObject =
              selectionSync.FindDocumentObjectByRuntime(scene, object)) {
        const MATH::Vec3 rotationEulerDeg =
            MATH::EulerXYZDegreesFromQuatNearest(
                result.gizmo.rotation,
                documentObject->transform.rotationEulerDeg);
        documentObject->transform = result.gizmo.transform;
        documentObject->transform.rotationEulerDeg = rotationEulerDeg;
        if (cameraPoseApplied) {
          documentObject->transform.scale = {1.0f, 1.0f, 1.0f};
        }
      }
      context.sceneDirty = true;
      scene.SetUnsavedSceneChanges(true);
    }
  }
}

void CinematicsWorkspaceController::ApplyCameraTimelineResult(
    DocumentSceneBase &scene, const CameraTimelinePanelResult &result,
    EditorContext &context, EditorWorkspaceHost &workspaceHost,
    bool affectsSceneDocument) {

  if (affectsSceneDocument && result.documentChanged) {
    context.sceneDirty = true;
    scene.SetUnsavedSceneChanges(true);
  }

  const bool canPreview =
      !scene.IsRuntimePlayActive() && result.previewEnabled &&
      result.evaluation.IsValid() &&
      IsEnabledCameraObject(scene, result.evaluation.cameraObjectId);
  if (!canPreview) {
    RestoreTimelinePreview(scene, workspaceHost);
    return;
  }

  if (!timelinePreviewOwned_) {
    timelineRestoreCameraObjectId_ = boundCameraObjectId_;
  }
  const bool shotChanged = timelinePreviewOwned_ &&
                           timelinePreviewShotId_ != result.evaluation.shotId;
  if (BindCameraPreview(scene, result.evaluation.cameraObjectId, workspaceHost,
                        CameraBlendFromTimelineResult(result), shotChanged)) {
    (void)scene.UpdateEditorCameraPreview(result.evaluation);
    timelinePreviewOwned_ = true;
    timelinePreviewShotId_ = result.evaluation.shotId;
  } else {
    RestoreTimelinePreview(scene, workspaceHost);
  }
}

bool CinematicsWorkspaceController::BindCameraPreview(
    DocumentSceneBase &scene, SceneObjectId cameraObjectId,
    EditorWorkspaceHost &workspaceHost, const CameraBlendDesc &blend,
    bool forceRebind) {

  if (scene.IsRuntimePlayActive() ||
      !IsEnabledCameraObject(scene, cameraObjectId)) {
    return false;
  }
  if (forceRebind || !scene.IsEditorCameraPreviewActive() ||
      !(scene.GetEditorCameraPreviewObjectId() == cameraObjectId)) {
    if (!scene.BeginEditorCameraPreview(cameraObjectId, blend, forceRebind)) {
      return false;
    }
  }

  cameraPreviewOwned_ = true;
  boundCameraObjectId_ = cameraObjectId;
  directorViewPanel_.SetTargetCamera(cameraObjectId);
  EditorViewInstance &gameView = workspaceHost.GetCinematicsGameView();
  gameView.cameraBinding.kind = EditorViewCameraSourceKind::SceneCameraObject;
  gameView.cameraBinding.sceneObjectId = cameraObjectId;
  return true;
}

void CinematicsWorkspaceController::RestoreTimelinePreview(
    DocumentSceneBase &scene, EditorWorkspaceHost &workspaceHost) {

  if (!timelinePreviewOwned_) {
    return;
  }
  const std::optional<SceneObjectId> restoreCamera =
      timelineRestoreCameraObjectId_;
  timelinePreviewOwned_ = false;
  timelinePreviewShotId_ = 0;
  timelineRestoreCameraObjectId_.reset();

  if (scene.IsRuntimePlayActive()) {
    cameraPreviewOwned_ = false;
    return;
  }
  if (restoreCamera && IsEnabledCameraObject(scene, *restoreCamera) &&
      BindCameraPreview(scene, *restoreCamera, workspaceHost)) {
    return;
  }
  if (cameraPreviewOwned_) {
    scene.EndEditorCameraPreview();
  }
  cameraPreviewOwned_ = false;
  ClearCameraBinding(workspaceHost);
}

void CinematicsWorkspaceController::ClearCameraBinding(
    EditorWorkspaceHost &workspaceHost) {

  boundCameraObjectId_.reset();
  EditorViewInstance &gameView = workspaceHost.GetCinematicsGameView();
  gameView.cameraBinding.kind = EditorViewCameraSourceKind::OwnedEditorCamera;
  gameView.cameraBinding.sceneObjectId = {};
}

} // namespace HIKARI::EDITOR
