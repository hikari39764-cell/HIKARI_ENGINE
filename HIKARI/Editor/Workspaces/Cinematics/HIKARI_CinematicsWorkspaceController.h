#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Documents/HIKARI_SequenceEditorDocumentController.h"
#include "Editor/Views/Director/HIKARI_DirectorViewPanel.h"
#include "Editor/Workspaces/Camera/Overview/HIKARI_CameraOverviewPanel.h"
#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelinePanel.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Workspaces/Sequence/HIKARI_SequenceEditorDocumentToolbar.h"
#include "Editor/Workspaces/Sequence/HIKARI_SequenceLibraryPanel.h"
#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

class DocumentSceneBase;
struct EditorContext;
class SelectionSyncService;

namespace EDITOR {
class EditorPlaySession;

struct CinematicsWorkspaceResult {
  bool toggleGamePreviewRequested = false;
  bool cinematicsChanged = false;
  bool saveSceneRequested = false;
  uint64_t timelineEditMergeId = 0;
  std::string statusMessage{};
};

class CinematicsWorkspaceController {
public:
  void DrawDockSpace(bool resetDefaultDockLayout) const;
  void SyncSceneIdentity(DocumentSceneBase &scene,
                         EditorWorkspaceHost &workspaceHost);
  void PrepareForRuntimePlay();
  void OnCinematicsDocumentRestored(DocumentSceneBase &scene,
                                    EditorWorkspaceHost &workspaceHost);
  void ApplyWorkspaceActivation(DocumentSceneBase &scene,
                                const EditorWorkspaceActivation &activation,
                                EditorContext &context,
                                EditorWorkspaceHost &workspaceHost);
  CinematicsWorkspaceResult Draw(DocumentSceneBase &scene,
                                 EditorPlaySession &playSession,
                                 EditorContext &context,
                                 SelectionSyncService &selectionSync,
                                 EditorWorkspaceHost &workspaceHost);
  bool RequestOpenSequenceAsset(DocumentSceneBase &scene,
                                const AssetGuid &assetGuid,
                                std::string &outMessage);
  bool IsEditingSequenceAsset() const noexcept;
  bool IsSequenceDocumentDirty() const noexcept;
  bool CanUndoSequenceDocument() const noexcept;
  bool CanRedoSequenceDocument() const noexcept;
  bool SaveSequenceDocument(DocumentSceneBase &scene, std::string &outMessage);
  bool UndoSequenceDocument(std::string &outMessage);
  bool RedoSequenceDocument(std::string &outMessage);
  void BindDocumentCommands(EditorCommandRouter &commandRouter,
                            DocumentSceneBase &scene);

private:
  void DrawDirectorViewWindow(DocumentSceneBase &scene, EditorContext &context,
                              SelectionSyncService &selectionSync,
                              EditorWorkspaceHost &workspaceHost);
  bool DrawGameViewWindow(DocumentSceneBase &scene,
                          EditorPlaySession &playSession,
                          EditorWorkspaceHost &workspaceHost);
  void DrawMapOverviewWindow(DocumentSceneBase &scene, EditorContext &context,
                             EditorWorkspaceHost &workspaceHost,
                             CinematicsWorkspaceResult &result);
  void DrawCameraListWindow(DocumentSceneBase &scene, EditorContext &context,
                            EditorWorkspaceHost &workspaceHost,
                            CinematicsWorkspaceResult &result);
  void DrawTimelineWindow(DocumentSceneBase &scene, EditorContext &context,
                          EditorWorkspaceHost &workspaceHost,
                          CinematicsWorkspaceResult &result);

  void ApplyCameraOverviewAction(DocumentSceneBase &scene,
                                 const CameraOverviewAction &action,
                                 EditorContext &context,
                                 EditorWorkspaceHost &workspaceHost,
                                 CinematicsWorkspaceResult &result);
  void ApplyDirectorViewResult(DocumentSceneBase &scene,
                               const DirectorViewPanelResult &result,
                               EditorContext &context,
                               SelectionSyncService &selectionSync);
  void ApplyCameraTimelineResult(DocumentSceneBase &scene,
                                 const CameraTimelinePanelResult &result,
                                 EditorContext &context,
                                 EditorWorkspaceHost &workspaceHost,
                                 bool affectsSceneDocument);
  bool BindCameraPreview(DocumentSceneBase &scene, SceneObjectId cameraObjectId,
                         EditorWorkspaceHost &workspaceHost,
                         const CameraBlendDesc &blend = {},
                         bool forceRebind = false);
  void RestoreTimelinePreview(DocumentSceneBase &scene,
                              EditorWorkspaceHost &workspaceHost);
  void ClearCameraBinding(EditorWorkspaceHost &workspaceHost);

  CameraOverviewPanel cameraOverviewPanel_{};
  CameraTimelinePanel cameraTimelinePanel_{};
  SequenceEditorDocumentController sequenceDocumentController_{};
  SequenceEditorDocumentToolbar sequenceDocumentToolbar_{};
  SequenceLibraryPanel sequenceLibraryPanel_{};
  DirectorViewPanel directorViewPanel_{};
  bool sequenceLibraryVisible_ = true;
  bool cameraPreviewOwned_ = false;
  bool timelinePreviewOwned_ = false;
  uint64_t timelinePreviewShotId_ = 0;
  std::optional<SceneObjectId> preWorkspacePreviewObjectId_{};
  std::optional<SceneObjectId> boundCameraObjectId_{};
  std::optional<SceneObjectId> timelineRestoreCameraObjectId_{};
  std::string sceneIdentity_{};
  std::string pendingStatusMessage_{};
  uint64_t sceneDocumentRevision_ = 0;
};

} // namespace EDITOR
} // namespace HIKARI
