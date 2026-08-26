#include "Editor/Documents/HIKARI_DialogueStyleEditorDocument.h"

#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"

namespace HIKARI::EDITOR {

    void DialogueStyleEditorDocument::New(
        const std::filesystem::path& projectRoot,
        const AssetDatabase& database) {
        style_ = DIALOGUE::MakeDefaultDialogueProjectStyle(projectRoot);
        DIALOGUE::ResolveDialogueProjectStyleAssets(style_, database);
        open_ = true;
        ResetHistory(false);
    }

    bool DialogueStyleEditorDocument::Open(
        const std::filesystem::path& projectRoot,
        const AssetDatabase& database,
        std::string& outMessage) {
        DIALOGUE::DialogueProjectStyle loaded{};
        if (!DIALOGUE::LoadDialogueProjectStyle(
                projectRoot,
                loaded,
                &outMessage)) {
            return false;
        }
        DIALOGUE::ResolveDialogueProjectStyleAssets(loaded, database);
        style_ = std::move(loaded);
        open_ = true;
        const bool alreadySaved = std::filesystem::is_regular_file(
            projectRoot / std::string(DIALOGUE::kDialogueProjectStylePath));
        ResetHistory(alreadySaved);
        return true;
    }

    bool DialogueStyleEditorDocument::Save(
        const std::filesystem::path& projectRoot,
        const AssetDatabase& database,
        std::string& outMessage) {
        DIALOGUE::NormalizeDialogueProjectStyle(style_);
        if (!DIALOGUE::SaveDialogueProjectStyle(
                projectRoot,
                style_,
                &outMessage)) {
            return false;
        }
        DIALOGUE::ResolveDialogueProjectStyleAssets(style_, database);
        savedHistoryCursor_ = historyCursor_;
        return true;
    }

    void DialogueStyleEditorDocument::DiscardChanges(
        const std::filesystem::path& projectRoot,
        const AssetDatabase& database) {
        std::string message{};
        if (Open(projectRoot, database, message)) {
            return;
        }
        style_ = DIALOGUE::MakeDefaultDialogueProjectStyle(projectRoot);
        DIALOGUE::ResolveDialogueProjectStyleAssets(style_, database);
        open_ = true;
        // Discard must allow workspace exit even when the project has never
        // saved a style file. The default projection is clean session state.
        ResetHistory(true);
    }

    DIALOGUE::DialogueProjectStyle&
        DialogueStyleEditorDocument::Style() noexcept {
        return style_;
    }

    const DIALOGUE::DialogueProjectStyle&
        DialogueStyleEditorDocument::Style() const noexcept {
        return style_;
    }

    bool DialogueStyleEditorDocument::IsOpen() const noexcept {
        return open_;
    }

    bool DialogueStyleEditorDocument::IsDirty() const noexcept {
        return open_ && (!savedHistoryCursor_ ||
            *savedHistoryCursor_ != historyCursor_);
    }

    bool DialogueStyleEditorDocument::CanUndo() const noexcept {
        return open_ && historyCursor_ > 0u;
    }

    bool DialogueStyleEditorDocument::CanRedo() const noexcept {
        return open_ && historyCursor_ < history_.size();
    }

    void DialogueStyleEditorDocument::RecordApplied(
        DIALOGUE::DialogueProjectStyle before,
        uint64_t mergeGroup) {
        if (historyCursor_ < history_.size()) {
            if (savedHistoryCursor_ &&
                *savedHistoryCursor_ > historyCursor_) {
                savedHistoryCursor_.reset();
            }
            history_.erase(history_.begin() + historyCursor_, history_.end());
        }
        const bool merge = mergeGroup != 0u && historyCursor_ > 0u &&
            historyCursor_ == history_.size() &&
            openMergeGroup_ == mergeGroup &&
            (!savedHistoryCursor_ ||
                *savedHistoryCursor_ != historyCursor_);
        if (merge) {
            history_.back().after = style_;
            return;
        }
        history_.push_back({ std::move(before), style_, mergeGroup });
        historyCursor_ = history_.size();
        openMergeGroup_ = mergeGroup;
    }

    void DialogueStyleEditorDocument::SealMerge() noexcept {
        openMergeGroup_ = 0u;
    }

    bool DialogueStyleEditorDocument::Undo() {
        if (!CanUndo()) return false;
        --historyCursor_;
        style_ = history_[historyCursor_].before;
        openMergeGroup_ = 0u;
        return true;
    }

    bool DialogueStyleEditorDocument::Redo() {
        if (!CanRedo()) return false;
        style_ = history_[historyCursor_].after;
        ++historyCursor_;
        openMergeGroup_ = 0u;
        return true;
    }

    void DialogueStyleEditorDocument::ResetHistory(bool saved) noexcept {
        history_.clear();
        historyCursor_ = 0u;
        savedHistoryCursor_ = saved
            ? std::optional<size_t>{ 0u }
            : std::nullopt;
        openMergeGroup_ = 0u;
    }

} // namespace HIKARI::EDITOR
