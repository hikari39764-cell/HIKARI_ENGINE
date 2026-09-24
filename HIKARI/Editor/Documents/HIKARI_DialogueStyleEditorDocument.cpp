#include "Editor/Documents/HIKARI_DialogueStyleEditorDocument.h"

#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"

namespace HIKARI::EDITOR {

void DialogueStyleEditorDocument::New() {
    style_ = DIALOGUE::MakeDefaultDialogueProjectStyle();
    open_ = true;
    ResetHistory(false);
}

bool DialogueStyleEditorDocument::Open(const std::filesystem::path& projectRoot, const AssetDatabase& database,
                                       std::string& outMessage) {
    DIALOGUE::DialogueProjectStyle loaded{};
    if (!DIALOGUE::LoadDialogueProjectStyle(projectRoot, loaded, &outMessage)) {
        return false;
    }
    DIALOGUE::ResolveDialogueProjectStyleAssets(loaded, database);
    style_ = std::move(loaded);
    open_ = true;
    // ファイル未作成時の既定スタイルも読み取り状態。閲覧だけで未保存にしない。
    ResetHistory(true);
    return true;
}

bool DialogueStyleEditorDocument::Save(const std::filesystem::path& projectRoot, const AssetDatabase& database,
                                       std::string& outMessage) {
    DIALOGUE::NormalizeDialogueProjectStyle(style_);
    if (!DIALOGUE::SaveDialogueProjectStyle(projectRoot, style_, &outMessage)) {
        return false;
    }
    DIALOGUE::ResolveDialogueProjectStyleAssets(style_, database);
    history_.MarkSaved();
    return true;
}

void DialogueStyleEditorDocument::DiscardChanges(const std::filesystem::path& projectRoot,
                                                 const AssetDatabase& database) {
    std::string message{};
    if (Open(projectRoot, database, message)) {
        return;
    }
    style_ = DIALOGUE::MakeDefaultDialogueProjectStyle();
    DIALOGUE::ResolveDialogueProjectStyleAssets(style_, database);
    open_ = true;
    // Discard must allow workspace exit even when the project has never
    // saved a style file. The default projection is clean session state.
    ResetHistory(true);
}

DIALOGUE::DialogueProjectStyle& DialogueStyleEditorDocument::Style() noexcept { return style_; }

const DIALOGUE::DialogueProjectStyle& DialogueStyleEditorDocument::Style() const noexcept { return style_; }

bool DialogueStyleEditorDocument::IsOpen() const noexcept { return open_; }

bool DialogueStyleEditorDocument::IsDirty() const noexcept { return open_ && history_.IsDirty(); }

bool DialogueStyleEditorDocument::CanUndo() const noexcept { return open_ && history_.CanUndo(); }

bool DialogueStyleEditorDocument::CanRedo() const noexcept { return open_ && history_.CanRedo(); }

void DialogueStyleEditorDocument::RecordApplied(DIALOGUE::DialogueProjectStyle before, uint64_t mergeGroup) {
    history_.Record(std::move(before), style_, mergeGroup);
}

void DialogueStyleEditorDocument::SealMerge() noexcept { history_.SealMerge(); }

bool DialogueStyleEditorDocument::Undo() { return open_ && history_.Undo(style_); }

bool DialogueStyleEditorDocument::Redo() { return open_ && history_.Redo(style_); }

void DialogueStyleEditorDocument::ResetHistory(bool saved) noexcept { history_.Reset(saved); }

} // namespace HIKARI::EDITOR
