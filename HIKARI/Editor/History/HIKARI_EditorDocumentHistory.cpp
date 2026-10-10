#include "Editor/History/HIKARI_EditorDocumentHistory.h"

#include <algorithm>
#include <utility>

namespace HIKARI::EDITOR {

namespace {
// 履歴の最大エントリ数を定義する
constexpr size_t kMaximumHistoryEntries = 128;

bool ContainsObject(const std::vector<SceneObjectId>& ids, SceneObjectId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

// 同じ物が別の種類（追加と変更など）に現れたら順序で結果が変わるので、部分投影を諦めて false を返す。
bool MergeObjectChanges(SceneObjectChanges& into, const SceneObjectChanges& next) {
    const auto append = [](std::vector<SceneObjectId>& target, const std::vector<SceneObjectId>& source,
                           const std::vector<SceneObjectId>& otherA, const std::vector<SceneObjectId>& otherB) {
        for (SceneObjectId id : source) {
            if (ContainsObject(otherA, id) || ContainsObject(otherB, id))
                return false;
            if (!ContainsObject(target, id))
                target.push_back(id);
        }
        return true;
    };
    return append(into.added, next.added, into.removed, into.changed) &&
           append(into.removed, next.removed, into.added, into.changed) &&
           append(into.changed, next.changed, into.added, into.removed);
}

// 一組の撤销・重做の結果を一歩ずつ合わせる。
void AppendHistoryStep(EditorHistoryResult& total, const IEditorDocumentCommand& command, bool undo) {
    std::optional<SceneObjectChanges> changes = command.GetObjectChanges(undo);
    if (!total.changed) {
        total = {command.GetImpact(), command.GetLabel(), true, std::move(changes)};
        return;
    }
    total.impact = total.impact | command.GetImpact();
    if (!total.objectChanges || !changes || !MergeObjectChanges(*total.objectChanges, *changes))
        total.objectChanges.reset();
}
} // namespace
// 履歴をリセットする
void EditorDocumentHistory::Reset(uint64_t documentRevision, bool initiallyDirty) {
    ++editRevision_;
    entries_.clear();
    cursor_ = 0;
    savedCursor_ = initiallyDirty ? std::optional<size_t>{} : std::optional<size_t>{0};
    documentRevision_ = documentRevision;
    openMergeGroup_ = 0;
    openUndoGroup_ = 0;
    undoGroupDepth_ = 0;
    initialDirty_ = initiallyDirty;
    bound_ = true;
}
// ドキュメントのリビジョン番号を同期する
bool EditorDocumentHistory::SyncDocumentRevision(uint64_t documentRevision, bool initiallyDirty) {
    if (bound_ && documentRevision_ == documentRevision) {
        return false;
    }
    Reset(documentRevision, initiallyDirty);
    return true;
}
// コマンドが適用されたことを履歴に記録する
void EditorDocumentHistory::RecordApplied(std::unique_ptr<IEditorDocumentCommand> command, uint64_t mergeGroup) {
    if (!command) {
        return;
    }
    ++editRevision_;
    if (cursor_ < entries_.size()) {
        if (savedCursor_ && *savedCursor_ > cursor_) {
            savedCursor_.reset();
        }
        entries_.erase(entries_.begin() + cursor_, entries_.end());
    }
    // マージグループが有効で、現在のマージグループと一致し、カーソルが最後のエントリを指している場合、マージを試みる
    const bool mayMerge = mergeGroup != 0 && openMergeGroup_ == mergeGroup && cursor_ > 0 &&
                          cursor_ == entries_.size() && entries_.back().mergeGroup == mergeGroup;
    if (mayMerge && entries_.back().command->TryMergeApplied(*command)) {
        return;
    }

    entries_.push_back({std::move(command), mergeGroup, openUndoGroup_});
    cursor_ = entries_.size();
    openMergeGroup_ = mergeGroup;
    if (entries_.size() > kMaximumHistoryEntries) {
        const size_t overflow = entries_.size() - kMaximumHistoryEntries;
        entries_.erase(entries_.begin(), entries_.begin() + overflow);
        cursor_ -= overflow;
        if (savedCursor_) {
            if (*savedCursor_ < overflow) {
                savedCursor_.reset();
            } else {
                *savedCursor_ -= overflow;
            }
        }
    }
}
// マージグループを閉じる
void EditorDocumentHistory::SealMerge() noexcept { openMergeGroup_ = 0; }
uint64_t EditorDocumentHistory::BeginUndoGroup(uint64_t reuse) noexcept {
    if (undoGroupDepth_++ == 0u) {
        SealMerge();
        openUndoGroup_ = reuse != 0u ? reuse : nextUndoGroup_++;
    }
    return openUndoGroup_;
}
uint64_t EditorDocumentHistory::GetUndoGroup() const noexcept {
    return CanUndo() ? entries_[cursor_ - 1].undoGroup : 0u;
}
void EditorDocumentHistory::EndUndoGroup() noexcept {
    if (undoGroupDepth_ == 0u || --undoGroupDepth_ != 0u) {
        return;
    }
    SealMerge();
    openUndoGroup_ = 0;
}
// Undo操作を行う。同じ組の記録は続けて戻す
EditorHistoryResult EditorDocumentHistory::Undo(SceneDocument& document) {
    SealMerge();
    if (!CanUndo()) {
        return {};
    }
    EditorHistoryResult result{};
    const uint64_t group = entries_[cursor_ - 1].undoGroup;
    do {
        Entry& entry = entries_[cursor_ - 1];
        entry.command->Undo(document);
        ++editRevision_;
        --cursor_;
        AppendHistoryStep(result, *entry.command, true);
    } while (group != 0u && CanUndo() && entries_[cursor_ - 1].undoGroup == group);
    return result;
}
EditorHistoryResult EditorDocumentHistory::UndoTo(SceneDocument& document, size_t depth) {
    SealMerge();
    EditorHistoryResult result{};
    while (cursor_ > depth) {
        Entry& entry = entries_[cursor_ - 1];
        entry.command->Undo(document);
        ++editRevision_;
        --cursor_;
        AppendHistoryStep(result, *entry.command, true);
    }
    return result;
}
// Redo操作を行う。同じ組の記録は続けて進める
EditorHistoryResult EditorDocumentHistory::Redo(SceneDocument& document) {
    SealMerge();
    if (!CanRedo()) {
        return {};
    }
    EditorHistoryResult result{};
    const uint64_t group = entries_[cursor_].undoGroup;
    do {
        Entry& entry = entries_[cursor_];
        entry.command->Redo(document);
        ++editRevision_;
        ++cursor_;
        AppendHistoryStep(result, *entry.command, false);
        result.label = entry.command->GetLabel();
    } while (group != 0u && CanRedo() && entries_[cursor_].undoGroup == group);
    return result;
}
// ドキュメントが保存されたことをマークする
void EditorDocumentHistory::MarkSaved() noexcept {
    SealMerge();
    savedCursor_ = cursor_;
    initialDirty_ = false;
}
// ドキュメントが未保存かどうかを判定する
bool EditorDocumentHistory::IsDirty() const noexcept {
    return initialDirty_ || !savedCursor_ || cursor_ != *savedCursor_;
}
// Undo操作が可能かどうかを判定する
bool EditorDocumentHistory::CanUndo() const noexcept { return cursor_ > 0; }
// Redo操作が可能かどうかを判定する
bool EditorDocumentHistory::CanRedo() const noexcept { return cursor_ < entries_.size(); }
// Undo操作のラベルを取得する
const std::string* EditorDocumentHistory::GetUndoLabel() const noexcept {
    return CanUndo() ? &entries_[cursor_ - 1].command->GetLabel() : nullptr;
}
// Redo操作のラベルを取得する
const std::string* EditorDocumentHistory::GetRedoLabel() const noexcept {
    return CanRedo() ? &entries_[cursor_].command->GetLabel() : nullptr;
}

} // namespace HIKARI::EDITOR
