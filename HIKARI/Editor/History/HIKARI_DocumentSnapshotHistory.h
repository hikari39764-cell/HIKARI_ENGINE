#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace HIKARI::EDITOR {

// 値型ドキュメントの履歴。検証・保存・実行時への反映はドキュメント側で行う。
template <class State, class Metadata = std::monostate>
class DocumentSnapshotHistory {
public:
    struct Entry {
        State before, after;
        Metadata metadata;
    };
    explicit DocumentSnapshotHistory(size_t capacity = 0) : capacity_(capacity) {}
    // Capacity zero means unbounded. Lowering a nonzero capacity preserves
    // the current cursor and trims the farthest past entries first, then redo.
    void SetCapacity(size_t capacity) {
        capacity_ = capacity;
        TrimToCapacity();
        SealMerge();
    }
    void Reset(bool saved = true) noexcept {
        entries_.clear();
        cursor_ = 0;
        savedCursor_ = saved ? std::optional<size_t>{0} : std::nullopt;
        SealMerge();
    }
    void MarkSaved() noexcept {
        savedCursor_ = cursor_;
        SealMerge();
    }
    bool IsDirty() const noexcept { return !savedCursor_ || *savedCursor_ != cursor_; }
    bool CanUndo() const noexcept { return cursor_ > 0; }
    bool CanRedo() const noexcept { return cursor_ < entries_.size(); }
    void SealMerge() noexcept { mergeGroup_ = 0; }
    const Entry* PeekUndo() const noexcept { return CanUndo() ? &entries_[cursor_ - 1] : nullptr; }
    const Entry* PeekRedo() const noexcept { return CanRedo() ? &entries_[cursor_] : nullptr; }
    bool CanMerge(uint64_t group) const noexcept {
        return group && mergeGroup_ == group && CanUndo() && !CanRedo() && (!savedCursor_ || *savedCursor_ != cursor_);
    }

    struct KeepMergedEntry {
        bool operator()(const State&, const State&) const { return false; }
    };
    template <class Equivalent = KeepMergedEntry>
    void Record(State before, const State& after, uint64_t mergeGroup = 0, Metadata metadata = {},
                Equivalent equivalent = {}) {
        const bool merge = CanMerge(mergeGroup);
        if (CanRedo()) {
            if (savedCursor_ && *savedCursor_ > cursor_)
                savedCursor_.reset();
            entries_.erase(entries_.begin() + cursor_, entries_.end());
        }
        if (merge) {
            entries_.back().after = after;
            entries_.back().metadata = std::move(metadata);
            if (equivalent(entries_.back().before, after)) {
                entries_.pop_back();
                cursor_ = entries_.size();
                SealMerge();
                return;
            }
        } else {
            entries_.push_back({std::move(before), after, std::move(metadata)});
            cursor_ = entries_.size();
        }
        mergeGroup_ = mergeGroup;
        TrimToCapacity();
    }
    bool Undo(State& state, Metadata* metadata = nullptr) {
        if (!CanUndo())
            return false;
        state = entries_[cursor_ - 1].before;
        if (metadata)
            *metadata = entries_[cursor_ - 1].metadata;
        --cursor_;
        SealMerge();
        return true;
    }
    bool Redo(State& state, Metadata* metadata = nullptr) {
        if (!CanRedo())
            return false;
        state = entries_[cursor_].after;
        if (metadata)
            *metadata = entries_[cursor_].metadata;
        ++cursor_;
        SealMerge();
        return true;
    }

private:
    void TrimToCapacity() {
        if (capacity_ == 0u) {
            return;
        }
        while (entries_.size() > capacity_) {
            if (cursor_ > 0u) {
                entries_.erase(entries_.begin());
                --cursor_;
                if (savedCursor_) {
                    if (*savedCursor_ == 0u) {
                        savedCursor_.reset();
                    } else {
                        --*savedCursor_;
                    }
                }
            } else {
                entries_.pop_back();
                if (savedCursor_ && *savedCursor_ > entries_.size()) {
                    savedCursor_.reset();
                }
            }
        }
    }

    std::vector<Entry> entries_;
    size_t capacity_ = 0;
    size_t cursor_ = 0;
    std::optional<size_t> savedCursor_{0};
    uint64_t mergeGroup_ = 0;
};

} // namespace HIKARI::EDITOR
