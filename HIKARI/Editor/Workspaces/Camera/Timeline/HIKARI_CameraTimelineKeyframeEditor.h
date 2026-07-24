#pragma once

#include <cstdint>
#include <vector>

#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineKeyframeDrag.h"
#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineSelection.h"
#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI::EDITOR {

struct CameraTimelineKeyframeLayout {
  float timelineLeft = 0.0f;
  float timelineRight = 0.0f;
  float transformTop = 0.0f;
  float transformBottom = 0.0f;
  float lensTop = 0.0f;
  float lensBottom = 0.0f;
  float scrollTimeSeconds = 0.0f;
  float pixelsPerSecond = 90.0f;
  float sequenceDurationSeconds = 0.0f;
  bool canvasHovered = false;
  bool editingAllowed = false;
  bool snapEnabled = true;
  float snapFramesPerSecond = 30.0f;
};

struct CameraTimelineKeyframeEditorResult {
  bool sequenceChanged = false;
  bool capturedLeftClick = false;
  bool selectionChanged = false;
};

class CameraTimelineKeyframeEditor {
public:
  CameraTimelineKeyframeEditorResult
  Draw(CinematicSequence &sequence, const CameraTimelineKeyframeLayout &layout);

  void Reset();
  void CancelInteraction();
  void ClearSelection();
  bool HasSelection() const noexcept;
  size_t GetSelectionCount() const noexcept;
  CameraTimelineKeyframeSelection GetSelection() const noexcept;
  const std::vector<CameraTimelineKeyframeSelection> &
  GetSelections() const noexcept;
  bool IsInteractionActive() const noexcept;
  bool SelectAll(CinematicSequence &sequence);
  void
  SetSelections(const std::vector<CameraTimelineKeyframeSelection> &selections);
  bool DeleteSelected(CinematicSequence &sequence);
  bool DrawSelectedKeyInspector(CinematicSequence &sequence,
                                bool editingAllowed, bool snapEnabled,
                                float snapFramesPerSecond);
  void SelectTransformKeyframe(SEQUENCER::SequenceBindingId bindingId,
                               uint64_t keyframeId) noexcept;
  void SelectLensKeyframe(SEQUENCER::SequenceBindingId bindingId,
                          uint64_t keyframeId) noexcept;

private:
  CameraTimelineSelectionSet selection_{};
  CameraTimelineKeyframeDragSession dragSession_{};
  float marqueeStartX_ = 0.0f;
  float marqueeStartY_ = 0.0f;
  CameraTimelineSelectionOperation marqueeOperation_ =
      CameraTimelineSelectionOperation::Replace;
  bool marqueeActive_ = false;
};

} // namespace HIKARI::EDITOR
