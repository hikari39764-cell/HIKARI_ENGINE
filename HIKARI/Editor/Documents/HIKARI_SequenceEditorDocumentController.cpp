#include "Editor/Documents/HIKARI_SequenceEditorDocumentController.h"

#include "Assets/HIKARI_AssetDatabase.h"

namespace HIKARI::EDITOR {

    SequenceEditorDocument&
        SequenceEditorDocumentController::GetDocument() noexcept {

        return document_;
    }

    const SequenceEditorDocument&
        SequenceEditorDocumentController::GetDocument() const noexcept {

        return document_;
    }

    bool SequenceEditorDocumentController::RequestOpenAsset(
        AssetDatabase& assetDatabase,
        const AssetGuid& assetGuid,
        std::string& outMessage) {

        if (!assetGuid.IsValid()) {
            outMessage = "Select a Sequence asset to open";
            return false;
        }
        if (document_.GetSourceKind() == SequenceEditorSourceKind::Asset &&
            document_.GetAssetGuid() == assetGuid) {
            outMessage = "Sequence asset is already open";
            return true;
        }
        return QueueOrApply(
            assetDatabase,
            { PendingSequenceDocumentActionKind::OpenAsset, assetGuid },
            outMessage);
    }

    bool SequenceEditorDocumentController::RequestNewAsset(
        AssetDatabase& assetDatabase,
        std::string& outMessage) {

        return QueueOrApply(
            assetDatabase,
            { PendingSequenceDocumentActionKind::NewAsset, {} },
            outMessage);
    }

    bool SequenceEditorDocumentController::RequestCloseToEmbedded(
        AssetDatabase& assetDatabase,
        std::string& outMessage) {

        if (document_.IsEmbeddedScene()) {
            outMessage = "Scene Cinematics is already open";
            return true;
        }
        return QueueOrApply(
            assetDatabase,
            { PendingSequenceDocumentActionKind::CloseToEmbedded, {} },
            outMessage);
    }

    bool SequenceEditorDocumentController::RequestRevert(
        AssetDatabase& assetDatabase,
        std::string& outMessage) {

        if (document_.GetSourceKind() != SequenceEditorSourceKind::Asset) {
            outMessage = "Only a saved Sequence asset can be reverted";
            return false;
        }
        return QueueOrApply(
            assetDatabase,
            { PendingSequenceDocumentActionKind::Revert, {} },
            outMessage);
    }

    bool SequenceEditorDocumentController::Save(
        AssetDatabase& assetDatabase,
        std::string& outMessage) {

        return document_.Save(assetDatabase, outMessage);
    }

    bool SequenceEditorDocumentController::SaveAs(
        AssetDatabase& assetDatabase,
        const CinematicSequence& sequence,
        std::string& outMessage) {

        const bool saved = document_.SaveAs(
            assetDatabase,
            sequence,
            outMessage);
        timelineResetRequested_ |= saved;
        return saved;
    }

    bool SequenceEditorDocumentController::Undo(std::string& outMessage) {
        if (!document_.Undo()) {
            outMessage = "Nothing to undo in the Sequence asset";
            return false;
        }
        timelineResetRequested_ = true;
        outMessage = "Undo: Edit Sequence asset";
        return true;
    }

    bool SequenceEditorDocumentController::Redo(std::string& outMessage) {
        if (!document_.Redo()) {
            outMessage = "Nothing to redo in the Sequence asset";
            return false;
        }
        timelineResetRequested_ = true;
        outMessage = "Redo: Edit Sequence asset";
        return true;
    }

    bool SequenceEditorDocumentController::HasPendingConfirmation()
        const noexcept {

        return pendingAction_.kind !=
            PendingSequenceDocumentActionKind::None;
    }

    const char* SequenceEditorDocumentController::GetPendingConfirmationText()
        const noexcept {

        switch (pendingAction_.kind) {
        case PendingSequenceDocumentActionKind::OpenAsset:
            return "Save changes before opening another Sequence asset?";
        case PendingSequenceDocumentActionKind::NewAsset:
            return "Save changes before creating a new Sequence asset?";
        case PendingSequenceDocumentActionKind::CloseToEmbedded:
            return "Save changes before returning to Scene Cinematics?";
        case PendingSequenceDocumentActionKind::Revert:
            return "Save changes before reverting the Sequence asset?";
        case PendingSequenceDocumentActionKind::None:
        default:
            return "Save changes to the current Sequence asset?";
        }
    }

    bool SequenceEditorDocumentController::ConfirmSaveThenContinue(
        AssetDatabase& assetDatabase,
        std::string& outMessage) {

        if (!HasPendingConfirmation()) {
            return false;
        }
        if (!Save(assetDatabase, outMessage)) {
            return false;
        }
        const PendingAction action = pendingAction_;
        pendingAction_ = {};
        return ApplyPending(assetDatabase, action, outMessage);
    }

    bool SequenceEditorDocumentController::ConfirmDiscardAndContinue(
        AssetDatabase& assetDatabase,
        std::string& outMessage) {

        if (!HasPendingConfirmation()) {
            return false;
        }
        const PendingAction action = pendingAction_;
        pendingAction_ = {};
        return ApplyPending(assetDatabase, action, outMessage);
    }

    void SequenceEditorDocumentController::CancelPending() noexcept {
        pendingAction_ = {};
    }

    bool SequenceEditorDocumentController::ConsumeTimelineResetRequested()
        noexcept {

        const bool requested = timelineResetRequested_;
        timelineResetRequested_ = false;
        return requested;
    }

    bool SequenceEditorDocumentController::QueueOrApply(
        AssetDatabase& assetDatabase,
        PendingAction action,
        std::string& outMessage) {

        if (document_.IsExternalDocument() && document_.IsDirty()) {
            pendingAction_ = action;
            outMessage = "Current Sequence asset has unsaved changes";
            return false;
        }
        return ApplyPending(assetDatabase, action, outMessage);
    }

    bool SequenceEditorDocumentController::ApplyPending(
        AssetDatabase& assetDatabase,
        PendingAction action,
        std::string& outMessage) {

        bool applied = false;
        switch (action.kind) {
        case PendingSequenceDocumentActionKind::OpenAsset:
            applied = document_.OpenAsset(
                assetDatabase,
                action.assetGuid,
                outMessage);
            break;
        case PendingSequenceDocumentActionKind::NewAsset:
            document_.NewAsset();
            outMessage = "Created a new unsaved Sequence asset";
            applied = true;
            break;
        case PendingSequenceDocumentActionKind::CloseToEmbedded:
            document_.OpenEmbeddedScene();
            outMessage = "Returned to Scene Cinematics";
            applied = true;
            break;
        case PendingSequenceDocumentActionKind::Revert:
            applied = document_.Revert(assetDatabase, outMessage);
            break;
        case PendingSequenceDocumentActionKind::None:
        default:
            break;
        }
        timelineResetRequested_ |= applied;
        return applied;
    }

} // namespace HIKARI::EDITOR
