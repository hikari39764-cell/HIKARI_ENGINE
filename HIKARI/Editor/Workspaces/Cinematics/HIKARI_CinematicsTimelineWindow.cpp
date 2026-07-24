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

void CinematicsWorkspaceController::DrawTimelineWindow(
    DocumentSceneBase &scene, EditorContext &context,
    EditorWorkspaceHost &workspaceHost, CinematicsWorkspaceResult &result) {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Timeline###Cinematics/Timeline", nullptr,
                    ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  AssetDatabase &assetDatabase = scene.GetAssetDatabase();
  SequenceEditorDocument &sequenceDocument =
      sequenceDocumentController_.GetDocument();
  SceneCinematicsSettings &initialSettings =
      sequenceDocument.GetSettings(scene.GetSceneDocument().cinematics);
  CinematicSequence *activeSequence =
      cameraTimelinePanel_.GetActiveSequence(initialSettings);
  const bool actionsAllowed = !scene.IsRuntimePlayActive();
  const SequenceEditorDocumentToolbarResult toolbarResult =
      sequenceDocumentToolbar_.Draw(sequenceDocumentController_, assetDatabase,
                                    activeSequence,
                                    scene.GetCurrentSceneDisplayName(),
                                    sequenceLibraryVisible_, actionsAllowed);
  if (toolbarResult.toggleLibraryRequested) {
    sequenceLibraryVisible_ = !sequenceLibraryVisible_;
  }
  result.saveSceneRequested |= toolbarResult.saveEmbeddedSceneRequested;
  if (toolbarResult.revealAssetRequested) {
    context.selection.ClearObjects();
    context.selection.selectedAsset = nullptr;
    context.selection.selectedAssetGuid = toolbarResult.revealAssetGuid.value;
    context.selection.selectedAssetPath =
        toolbarResult.revealAssetPath.generic_string();
    sequenceLibraryPanel_.Select(toolbarResult.revealAssetGuid);
  }
  if (!toolbarResult.statusMessage.empty()) {
    result.statusMessage = toolbarResult.statusMessage;
  }
  ImGui::Separator();

  if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
    cameraTimelinePanel_.ResetForScene();
  }

  if (sequenceLibraryVisible_) {
    ImGui::BeginChild("##CinematicsSequenceLibrary", ImVec2(300.0f, 0.0f),
                      ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX);
    const SequenceLibraryPanelResult libraryResult = sequenceLibraryPanel_.Draw(
        assetDatabase, sequenceDocument.GetAssetGuid(), actionsAllowed);
    ImGui::EndChild();
    if (libraryResult.action == SequenceLibraryActionKind::NewAsset) {
      (void)sequenceDocumentController_.RequestNewAsset(assetDatabase,
                                                        result.statusMessage);
    } else if (libraryResult.action == SequenceLibraryActionKind::OpenAsset) {
      (void)RequestOpenSequenceAsset(scene, libraryResult.assetGuid,
                                     result.statusMessage);
    }
    ImGui::SameLine();
  }

  ImGui::BeginChild("##CinematicsSequenceTimeline", ImVec2(0.0f, 0.0f),
                    ImGuiChildFlags_None);
  if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
    cameraTimelinePanel_.ResetForScene();
  }
  SceneCinematicsSettings &editableSettings =
      sequenceDocument.GetSettings(scene.GetSceneDocument().cinematics);
  const bool editingEmbedded = sequenceDocument.IsEmbeddedScene();
  std::optional<SceneCinematicsSettings> assetBefore{};
  if (!editingEmbedded && actionsAllowed) {
    assetBefore = editableSettings;
  }
  SceneObjectId selectedCameraObjectId{};
  if (context.selection.selectedObject != nullptr &&
      context.selection.selectedObject->GetComponent<CameraComponent>() !=
          nullptr) {
    selectedCameraObjectId = context.selection.selectedObject->GetDocumentId();
  }
  const CameraTimelinePanelResult panelResult = cameraTimelinePanel_.Draw(
      scene.GetSceneDocument(), editableSettings,
      sequenceDocument.GetAssetGuid(), selectedCameraObjectId,
      ImGui::GetIO().DeltaTime, actionsAllowed, editingEmbedded);
  if (editingEmbedded) {
    result.cinematicsChanged |= panelResult.documentChanged;
    result.timelineEditMergeId = panelResult.editMergeId;
  } else if (panelResult.documentChanged && assetBefore) {
    sequenceDocument.RecordApplied(std::move(*assetBefore),
                                   panelResult.editMergeId);
  } else if (panelResult.editMergeId == 0) {
    sequenceDocument.SealMerge();
  }
  ApplyCameraTimelineResult(scene, panelResult, context, workspaceHost,
                            editingEmbedded);
  ImGui::EndChild();

  sequenceDocumentToolbar_.DrawPendingConfirmation(
      sequenceDocumentController_, assetDatabase, result.statusMessage);
  ImGui::End();
#else
  (void)scene;
  (void)context;
  (void)workspaceHost;
  (void)result;
#endif
}

} // namespace HIKARI::EDITOR
