#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"

#include <algorithm>
#include <cmath>

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Authoring/HIKARI_SequencePreviewBindingResolver.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/Views/Director/HIKARI_DirectorCameraPose.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI::EDITOR {

bool CinematicsWorkspaceController::RequestOpenSequenceAsset(DocumentSceneBase& scene, const AssetGuid& assetGuid,
                                                             std::string& outMessage) {
    const bool opened = sequenceDocumentController_.RequestOpenAsset(scene.GetAssetDatabase(), assetGuid, outMessage);
    sequenceLibraryPanel_.Select(assetGuid);
    if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
        ResetEditing(scene);
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

bool CinematicsWorkspaceController::SaveSequenceDocument(DocumentSceneBase& scene, std::string& outMessage) {
    const bool saved = sequenceDocumentController_.Save(scene.GetAssetDatabase(), outMessage);
    if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
        ResetEditing(scene);
    }
    return saved;
}

bool CinematicsWorkspaceController::UndoSequenceDocument(std::string& outMessage) {
    const bool changed = sequenceDocumentController_.Undo(outMessage);
    if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
        cursor_.Pause();
        previewChanged_ = true;
        selectedId_ = 0;
    }
    return changed;
}

bool CinematicsWorkspaceController::RedoSequenceDocument(std::string& outMessage) {
    const bool changed = sequenceDocumentController_.Redo(outMessage);
    if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
        cursor_.Pause();
        previewChanged_ = true;
        selectedId_ = 0;
    }
    return changed;
}

bool CinematicsWorkspaceController::RequestNewSequenceAsset(DocumentSceneBase& scene, std::string& outMessage) {
    const bool requested = sequenceDocumentController_.RequestNewAsset(scene.GetAssetDatabase(), outMessage);
    if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
        ResetEditing(scene);
    }
    return requested;
}

bool CinematicsWorkspaceController::RequestRevertSequenceAsset(DocumentSceneBase& scene, std::string& outMessage) {
    const bool requested = sequenceDocumentController_.RequestRevert(scene.GetAssetDatabase(), outMessage);
    if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
        ResetEditing(scene);
    }
    return requested;
}

bool CinematicsWorkspaceController::RequestCloseSequenceAsset(DocumentSceneBase& scene, std::string& outMessage) {
    const bool requested = sequenceDocumentController_.RequestCloseToEmbedded(scene.GetAssetDatabase(), outMessage);
    if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
        ResetEditing(scene);
    }
    return requested;
}

bool CinematicsWorkspaceController::HasPendingSequenceConfirmation() const noexcept {
    return sequenceDocumentController_.HasPendingConfirmation();
}

const SequenceEditorDocument& CinematicsWorkspaceController::GetSequenceDocument() const noexcept {
    return sequenceDocumentController_.GetDocument();
}

void CinematicsWorkspaceController::BindDocumentCommands(EditorCommandRouter& commandRouter, DocumentSceneBase& scene) {
    (void)scene;
    commandRouter.BindOperation(EditorCommandId::SaveDocument, TEXT::Localize("ui.cinematics.save"),
                                IsEditingSequenceAsset(), "cinematics.save");
    commandRouter.BindOperation(EditorCommandId::Undo, TEXT::Localize("ui.cinematics.undo"), CanUndoSequenceDocument(),
                                "cinematics.undo");
    commandRouter.BindOperation(EditorCommandId::Redo, TEXT::Localize("ui.cinematics.redo"), CanRedoSequenceDocument(),
                                "cinematics.redo");
}

void CinematicsWorkspaceController::ResetEditing(DocumentSceneBase& scene) {
    if (ownsPreview_) {
        scene.EndSequencePreview();
        ownsPreview_ = false;
    }
    cursor_.Clear();
    selectedId_ = 0;
    curveView_ = {};
    stageInitialized_ = false;
    previewEnabled_ = false;
    previewChanged_ = true;
    previewBindings_.Clear();
    if (IsEditingSequenceAsset())
        RestoreSequencePreviewBindings(scene.GetSceneDocument(), GetSequenceDocument().GetAssetGuid(),
                                       ActiveSequence(scene), previewBindings_);
}

void CinematicsWorkspaceController::PrepareForRuntimePlay(DocumentSceneBase& scene) {
    if (ownsPreview_) {
        scene.EndSequencePreview();
        ownsPreview_ = false;
    }
    cursor_.Pause();
    previewEnabled_ = false;
    directorViewPanel_.ExitPilot();
}

void CinematicsWorkspaceController::OnCinematicsDocumentRestored(DocumentSceneBase& scene, EditorWorkspaceHost&) {
    if (ownsPreview_) {
        scene.EndSequencePreview();
        ownsPreview_ = false;
    }
    cursor_.Pause();
    previewChanged_ = true;
}

void CinematicsWorkspaceController::ApplyWorkspaceActivation(DocumentSceneBase& scene,
                                                             const EditorWorkspaceActivation& activation,
                                                             EditorContext&, EditorWorkspaceHost&) {
    if (activation.previous == EditorWorkspaceId::Cinematics && activation.current != EditorWorkspaceId::Cinematics) {
        if (ownsPreview_) {
            scene.EndSequencePreview();
            ownsPreview_ = false;
        }
        cursor_.Pause();
        previewEnabled_ = false;
        directorViewPanel_.LeaveWorkspace();
    }
    if (activation.current == EditorWorkspaceId::Cinematics) {
        const auto& sceneDocument = scene.GetSceneDocument();
        const bool emptyEmbedded =
            std::all_of(sceneDocument.cinematics.sequences.begin(), sceneDocument.cinematics.sequences.end(),
                        [](const auto& sequence) { return GetCinematicSequenceContentEnd(sequence) == 0; });
        if (!IsEditingSequenceAsset() && emptyEmbedded) {
            AssetGuid candidate;
            bool ambiguous = false;
            for (const auto& object : sceneDocument.objects)
                for (const auto& component : object.components) {
                    if (component.type != "SequencePlayerComponent" || !component.properties.is_object())
                        continue;
                    const auto value = component.properties.find("sequenceAssetGuid");
                    if (value == component.properties.end() || !value->is_string() ||
                        value->get_ref<const std::string&>().empty())
                        continue;
                    const auto guid = value->get<std::string>();
                    if (candidate.IsValid() && candidate.value != guid)
                        ambiguous = true;
                    candidate.value = guid;
                }
            if (candidate.IsValid() && !ambiguous)
                RequestOpenSequenceAsset(scene, candidate, pendingStatusMessage_);
        }
        const auto cameraId = scene.GetSceneDocument().camera.defaultCameraObjectId.value_or(SceneObjectId{});
        directorViewPanel_.FrameTargetCamera(scene, cameraId);
        directorViewPanel_.SetTargetCamera({});
    }
}

void CinematicsWorkspaceController::SyncSceneIdentity(DocumentSceneBase& scene, EditorWorkspaceHost&) {
    const auto& guid = scene.GetCurrentSceneAssetGuid();
    const std::string identity = guid.IsValid() ? guid.value : scene.GetSceneId();
    if (sceneIdentity_ != identity || sceneDocumentRevision_ != scene.GetSceneDocumentRevision()) {
        ResetEditing(scene);
        directorViewPanel_.ResetForScene();
        sceneIdentity_ = identity;
        sceneDocumentRevision_ = scene.GetSceneDocumentRevision();
    }
}

CinematicSequence& CinematicsWorkspaceController::ActiveSequence(DocumentSceneBase& scene) {
    auto& settings = sequenceDocumentController_.GetDocument().GetSettings(scene.GetSceneDocument().cinematics);
    auto* sequence = FindCinematicSequence(settings, activeSequenceId_);
    if (!sequence) {
        if (settings.sequences.empty())
            settings.sequences.push_back({});
        sequence = &settings.sequences.front();
        activeSequenceId_ = sequence->id;
    }
    return *sequence;
}

SEQUENCER::CameraCutClip* CinematicsWorkspaceController::SelectedShot(CinematicSequence& sequence) {
    if (selectionKind_ != SelectionKind::Shot)
        return nullptr;
    for (auto& shot : sequence.cameraCutTrack.clips)
        if (shot.id == selectedId_)
            return &shot;
    return nullptr;
}

void CinematicsWorkspaceController::CreateShot(CinematicSequence& sequence, const Camera3D& camera) {
    SEQUENCER::CameraCutClip shot;
    shot.id = SEQUENCER::AllocateCameraCutClipId(sequence.cameraCutTrack);
    shot.startTimeSeconds = cursor_.GetTimeSeconds();
    for (const auto& existing : sequence.cameraCutTrack.clips)
        if (shot.startTimeSeconds >= existing.startTimeSeconds &&
            shot.startTimeSeconds < existing.startTimeSeconds + existing.durationSeconds)
            shot.startTimeSeconds = existing.startTimeSeconds + existing.durationSeconds;
    shot.durationSeconds = 3.0f;
    shot.camera.owned = true;
    shot.camera.name = "Shot " + std::to_string(shot.id);
    SEQUENCER::SetShotPose(shot.camera, SEQUENCER::CaptureShotPose(camera), false);
    shot.camera.nearClip = camera.GetNearZ();
    shot.camera.farClip = camera.GetFarZ();
    sequence.cameraCutTrack.clips.push_back(shot);
    selectedId_ = shot.id;
    selectionKind_ = SelectionKind::Shot;
    previewEnabled_ = true;
    previewChanged_ = true;
    sequence.durationSeconds = (std::max)(sequence.durationSeconds, shot.startTimeSeconds + shot.durationSeconds);
    cursor_.SetDuration(sequence.durationSeconds);
    cursor_.Seek(shot.startTimeSeconds);
}

void CinematicsWorkspaceController::CapturePose(DocumentSceneBase& scene, CinematicSequence& sequence,
                                                const Camera3D& camera) {
    auto* shot = SelectedShot(sequence);
    if (!shot) {
        CreateShot(sequence, camera);
        return;
    }
    shot->camera.owned = true;
    const float local = std::clamp(cursor_.GetTimeSeconds() - shot->startTimeSeconds, 0.0f, shot->durationSeconds);
    SceneObjectId anchorId;
    const auto* anchor = previewBindings_.Resolve(sequence.bindings, shot->camera.anchor, anchorId)
                             ? scene.GetWorld().FindObject(anchorId)
                             : nullptr;
    const auto anchorMatrix = anchor ? anchor->GetTransform().GetWorldMatrix() : MATH::Mat4::Identity();
    const float keyTime = autoKey_ ? local : 0.0f;
    const auto pose = SEQUENCER::CaptureShotPose(camera, keyTime, anchor ? &anchorMatrix : nullptr);
    SEQUENCER::SetShotPose(shot->camera, pose, autoKey_);
    SEQUENCER::NormalizeShotCamera(shot->camera);
    previewChanged_ = true;
    previewEnabled_ = true;
}

bool CinematicsWorkspaceController::IsDocumentOpen() const noexcept {
    return IsEditingSequenceAsset() || IsSequenceDocumentDirty();
}

nlohmann::json CinematicsWorkspaceController::DescribeDocumentState() const {
    const SequenceEditorDocument& document = GetSequenceDocument();
    return {
        {"open", IsDocumentOpen()},
        {"assetGuid", document.GetAssetGuid().value},
        {"name", document.GetAssetSequence() ? document.GetAssetSequence()->name : std::string{}},
        {"dirty", IsSequenceDocumentDirty()},
        {"previewScope",
         "Sequence camera at requested time over the current scene; scene simulation and sequence post effects are not sampled."}};
}

} // namespace HIKARI::EDITOR
