#pragma once

#include <cstdint>

#include "Scene/HIKARI_CinematicSequence.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

struct CameraTimelineShotLayout {
  float timelineLeft = 0.0f;
  float timelineRight = 0.0f;
  float trackTop = 0.0f;
  float trackBottom = 0.0f;
  float scrollTimeSeconds = 0.0f;
  float pixelsPerSecond = 90.0f;
  bool canvasHovered = false;
  bool editingAllowed = false;
  bool snapEnabled = true;
  int snapFramesPerSecond = 30;
};

struct CameraTimelineShotEditorResult {
  bool sequenceChanged = false;
  bool capturedLeftClick = false;
  bool selectionChanged = false;
};

class CameraTimelineShotEditor {
public:
  CameraTimelineShotEditorResult
  Draw(const SceneDocument &document, CinematicSequence &sequence,
       const SEQUENCER::SequenceBindingContext *previewBindings,
       const CameraTimelineShotLayout &layout);

  void Reset();
  void CancelInteraction();
  uint64_t GetSelectedShotId() const noexcept;
  void SetSelectedShotId(uint64_t shotId) noexcept;
  void ClearSelection() noexcept;

private:
  enum class DragMode : uint8_t {
    None,
    Move,
    ResizeLeft,
    ResizeRight,
  };

  uint64_t selectedShotId_ = 0;
  uint64_t draggedShotId_ = 0;
  DragMode dragMode_ = DragMode::None;
  float dragStartMouseX_ = 0.0f;
  float dragStartShotTime_ = 0.0f;
  float dragStartShotDuration_ = 0.0f;
};

} // namespace HIKARI::EDITOR
