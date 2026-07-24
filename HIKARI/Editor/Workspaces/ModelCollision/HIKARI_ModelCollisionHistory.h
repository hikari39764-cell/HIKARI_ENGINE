#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "Assets/Collision/HIKARI_ModelCollisionSetup.h"

namespace HIKARI::EDITOR {

class ModelCollisionHistory {
public:
  void Reset(const ASSETS::COLLISION::ModelCollisionSetup &setup);
  void Commit(const ASSETS::COLLISION::ModelCollisionSetup &setup,
              std::string label);

  bool CanUndo() const noexcept;
  bool CanRedo() const noexcept;
  bool Undo(ASSETS::COLLISION::ModelCollisionSetup &outSetup);
  bool Redo(ASSETS::COLLISION::ModelCollisionSetup &outSetup);

  void MarkSaved() noexcept;
  bool IsDirty() const noexcept;
  const std::string *GetUndoLabel() const noexcept;
  const std::string *GetRedoLabel() const noexcept;

private:
  struct Entry {
    ASSETS::COLLISION::ModelCollisionSetup setup{};
    std::string label{};
  };

  static constexpr size_t kMaximumEntries = 128u;
  std::vector<Entry> entries_{};
  size_t cursor_ = 0u;
  size_t savedCursor_ = 0u;
};

} // namespace HIKARI::EDITOR
