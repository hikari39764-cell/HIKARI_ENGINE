#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionHistory.h"

namespace HIKARI::EDITOR {

void ModelCollisionHistory::Reset(
    const ASSETS::COLLISION::ModelCollisionSetup &setup) {

  entries_.clear();
  entries_.push_back({setup, {}});
  cursor_ = 0u;
  savedCursor_ = 0u;
}

void ModelCollisionHistory::Commit(
    const ASSETS::COLLISION::ModelCollisionSetup &setup, std::string label) {

  if (entries_.empty()) {
    Reset(setup);
    return;
  }
  if (cursor_ + 1u < entries_.size()) {
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_ + 1u),
                   entries_.end());
    if (savedCursor_ > cursor_) {
      savedCursor_ = static_cast<size_t>(-1);
    }
  }
  entries_.push_back({setup, std::move(label)});
  cursor_ = entries_.size() - 1u;
  if (entries_.size() > kMaximumEntries) {
    entries_.erase(entries_.begin());
    --cursor_;
    if (savedCursor_ != static_cast<size_t>(-1)) {
      savedCursor_ =
          savedCursor_ == 0u ? static_cast<size_t>(-1) : savedCursor_ - 1u;
    }
  }
}

bool ModelCollisionHistory::CanUndo() const noexcept {
  return !entries_.empty() && cursor_ > 0u;
}

bool ModelCollisionHistory::CanRedo() const noexcept {
  return !entries_.empty() && cursor_ + 1u < entries_.size();
}

bool ModelCollisionHistory::Undo(
    ASSETS::COLLISION::ModelCollisionSetup &outSetup) {

  if (!CanUndo()) {
    return false;
  }
  --cursor_;
  outSetup = entries_[cursor_].setup;
  return true;
}

bool ModelCollisionHistory::Redo(
    ASSETS::COLLISION::ModelCollisionSetup &outSetup) {

  if (!CanRedo()) {
    return false;
  }
  ++cursor_;
  outSetup = entries_[cursor_].setup;
  return true;
}

void ModelCollisionHistory::MarkSaved() noexcept { savedCursor_ = cursor_; }

bool ModelCollisionHistory::IsDirty() const noexcept {
  return savedCursor_ != cursor_;
}

const std::string *ModelCollisionHistory::GetUndoLabel() const noexcept {
  if (!CanUndo()) {
    return nullptr;
  }
  return &entries_[cursor_].label;
}

const std::string *ModelCollisionHistory::GetRedoLabel() const noexcept {
  if (!CanRedo()) {
    return nullptr;
  }
  return &entries_[cursor_ + 1u].label;
}

} // namespace HIKARI::EDITOR
