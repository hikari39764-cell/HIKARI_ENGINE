#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineSelection.h"

#include <algorithm>
#include <utility>

namespace HIKARI::EDITOR {

bool CameraTimelineKeyframeSelection::IsValid() const noexcept {
  return kind != CameraTimelineKeyframeKind::None && bindingId.IsValid() &&
         keyframeId != 0;
}

bool CameraTimelineSelectionSet::Empty() const noexcept {
  return items_.empty();
}

size_t CameraTimelineSelectionSet::Size() const noexcept {
  return items_.size();
}

bool CameraTimelineSelectionSet::Contains(
    const CameraTimelineKeyframeSelection &selection) const noexcept {

  return std::find(items_.begin(), items_.end(), selection) != items_.end();
}

const std::vector<CameraTimelineKeyframeSelection> &
CameraTimelineSelectionSet::Items() const noexcept {

  return items_;
}

CameraTimelineKeyframeSelection
CameraTimelineSelectionSet::Primary() const noexcept {

  return items_.empty() ? CameraTimelineKeyframeSelection{} : items_.back();
}

bool CameraTimelineSelectionSet::Clear() noexcept {
  if (items_.empty()) {
    return false;
  }
  items_.clear();
  return true;
}

bool CameraTimelineSelectionSet::Apply(
    const CameraTimelineKeyframeSelection &selection,
    CameraTimelineSelectionOperation operation) {

  if (!selection.IsValid()) {
    return operation == CameraTimelineSelectionOperation::Replace ? Clear()
                                                                  : false;
  }
  return Apply(std::vector<CameraTimelineKeyframeSelection>{selection},
               operation);
}

bool CameraTimelineSelectionSet::Apply(
    const std::vector<CameraTimelineKeyframeSelection> &selections,
    CameraTimelineSelectionOperation operation) {

  bool changed = false;
  if (operation == CameraTimelineSelectionOperation::Replace) {
    std::vector<CameraTimelineKeyframeSelection> unique{};
    unique.reserve(selections.size());
    for (const CameraTimelineKeyframeSelection &selection : selections) {
      if (selection.IsValid() &&
          std::find(unique.begin(), unique.end(), selection) == unique.end()) {
        unique.push_back(selection);
      }
    }
    changed = unique != items_;
    items_ = std::move(unique);
    return changed;
  }

  for (const CameraTimelineKeyframeSelection &selection : selections) {
    if (!selection.IsValid()) {
      continue;
    }
    const auto found = std::find(items_.begin(), items_.end(), selection);
    if (operation == CameraTimelineSelectionOperation::Toggle) {
      if (found != items_.end()) {
        items_.erase(found);
      } else {
        items_.push_back(selection);
      }
      changed = true;
    } else if (found == items_.end()) {
      items_.push_back(selection);
      changed = true;
    }
  }
  return changed;
}

bool CameraTimelineSelectionSet::Remove(
    const CameraTimelineKeyframeSelection &selection) noexcept {

  const auto found = std::find(items_.begin(), items_.end(), selection);
  if (found == items_.end()) {
    return false;
  }
  items_.erase(found);
  return true;
}

} // namespace HIKARI::EDITOR
