#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"

#include <algorithm>
#include <cmath>

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Authoring/HIKARI_SequencePreviewBindingResolver.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

bool CinematicsWorkspaceController::ConfigureCameraEditing(DocumentSceneBase& scene, const nlohmann::json& parameters,
                                                           std::string& message) {
    auto& document = sequenceDocumentController_.GetDocument();
    if (!document.RefreshCleanSource(scene.GetAssetDatabase(), message))
        return false;
    requestedEndpoint_ = parameters.value("end", false);
    previewRate_ = std::clamp(parameters.value("speed", 1.f), .25f, 4.f);
    RestoreSequencePreviewBindings(scene.GetSceneDocument(), document.GetAssetGuid(), ActiveSequence(scene),
                                   previewBindings_);
    message = "已打开镜头编辑与预览。";
    return true;
}

bool CinematicsWorkspaceController::RequestOpenSequenceAsset(DocumentSceneBase& scene, const AssetGuid& assetGuid,
                                                             std::string& outMessage) {
    const bool opened =
        sequenceDocumentController_.RequestOpenAsset(scene.GetAssetDatabase(), assetGuid, outMessage) &&
        sequenceDocumentController_.GetDocument().RefreshCleanSource(scene.GetAssetDatabase(), outMessage);
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
    selectedObjectKey_ = -1;
    curveView_ = {};
    curveGestureBefore_.reset();
    selectionPreviewRange_.reset();
    stageInitialized_ = false;
    pilot_ = false;
    selectedRailPoint_ = -1;
    fitTimelineRequested_ = true;
    autoPreview_ = true;
    previewEnabled_ = false;
    previewChanged_ = true;
    previewBindings_.Clear();
    previewDiagnostic_.clear();
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
        // 他の演出の保存で更新された端点を再表示する。未保存の草稿は常に保持する。
        std::string message;
        if (!sequenceDocumentController_.GetDocument().RefreshCleanSource(scene.GetAssetDatabase(), message))
            pendingStatusMessage_ = std::move(message);
        previewChanged_ = true;
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
