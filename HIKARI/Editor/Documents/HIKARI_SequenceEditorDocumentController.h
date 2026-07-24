#pragma once

#include <string>

#include "Editor/Documents/HIKARI_SequenceEditorDocument.h"

namespace HIKARI {

    class AssetDatabase;

namespace EDITOR {

    enum class PendingSequenceDocumentActionKind : uint8_t {
        None,
        OpenAsset,
        NewAsset,
        CloseToEmbedded,
        Revert,
    };

    class SequenceEditorDocumentController {
    public:
        SequenceEditorDocument& GetDocument() noexcept;
        const SequenceEditorDocument& GetDocument() const noexcept;

        bool RequestOpenAsset(
            AssetDatabase& assetDatabase,
            const AssetGuid& assetGuid,
            std::string& outMessage);
        bool RequestNewAsset(
            AssetDatabase& assetDatabase,
            std::string& outMessage);
        bool RequestCloseToEmbedded(
            AssetDatabase& assetDatabase,
            std::string& outMessage);
        bool RequestRevert(
            AssetDatabase& assetDatabase,
            std::string& outMessage);

        bool Save(
            AssetDatabase& assetDatabase,
            std::string& outMessage);
        bool SaveAs(
            AssetDatabase& assetDatabase,
            const CinematicSequence& sequence,
            std::string& outMessage);
        bool Undo(std::string& outMessage);
        bool Redo(std::string& outMessage);

        bool HasPendingConfirmation() const noexcept;
        const char* GetPendingConfirmationText() const noexcept;
        bool ConfirmSaveThenContinue(
            AssetDatabase& assetDatabase,
            std::string& outMessage);
        bool ConfirmDiscardAndContinue(
            AssetDatabase& assetDatabase,
            std::string& outMessage);
        void CancelPending() noexcept;

        bool ConsumeTimelineResetRequested() noexcept;

    private:
        struct PendingAction {
            PendingSequenceDocumentActionKind kind =
                PendingSequenceDocumentActionKind::None;
            AssetGuid assetGuid{};
        };

        bool QueueOrApply(
            AssetDatabase& assetDatabase,
            PendingAction action,
            std::string& outMessage);
        bool ApplyPending(
            AssetDatabase& assetDatabase,
            PendingAction action,
            std::string& outMessage);

        SequenceEditorDocument document_{};
        PendingAction pendingAction_{};
        bool timelineResetRequested_ = false;
    };

} // namespace EDITOR
} // namespace HIKARI
