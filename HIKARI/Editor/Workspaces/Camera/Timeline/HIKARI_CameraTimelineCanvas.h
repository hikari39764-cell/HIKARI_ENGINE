#pragma once

#include <cstdint>

#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineKeyframeClipboard.h"
#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineKeyframeEditor.h"
#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineShotEditor.h"
#include "Scene/HIKARI_CinematicSequence.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

struct CameraTimelineCanvasResult {
  bool sequenceChanged = false;
  bool playheadChanged = false;
  float playheadTimeSeconds = 0.0f;
};

class CameraTimelineCanvas {
public:
  CameraTimelineCanvasResult
  Draw(const SceneDocument &document, CinematicSequence &sequence,
       float playheadTimeSeconds, bool playing, bool editingAllowed,
       const SEQUENCER::SequenceBindingContext *previewBindings = nullptr);

  void Reset();
  void CancelInteraction();
  uint64_t GetSelectedShotId() const noexcept;
  void SetSelectedShotId(uint64_t shotId) noexcept;
  float GetPixelsPerSecond() const noexcept;
  void SetPixelsPerSecond(float pixelsPerSecond) noexcept;
  bool IsSnapEnabled() const noexcept;
  void SetSnapEnabled(bool enabled) noexcept;
  int GetSnapFramesPerSecond() const noexcept;
  void SetSnapFramesPerSecond(int framesPerSecond) noexcept;
  bool HasSelectedKeyframe() const noexcept;
  size_t GetSelectedKeyframeCount() const noexcept;
  bool DeleteSelectedKeyframe(CinematicSequence &sequence);
  bool DrawSelectedKeyframeInspector(CinematicSequence &sequence,
                                     bool editingAllowed);
  void SelectTransformKeyframe(SEQUENCER::SequenceBindingId bindingId,
                               uint64_t keyframeId) noexcept;
  void SelectLensKeyframe(SEQUENCER::SequenceBindingId bindingId,
                          uint64_t keyframeId) noexcept;

private:
  float pixelsPerSecond_ = 90.0f;
  float scrollTimeSeconds_ = 0.0f;
  float panStartMouseX_ = 0.0f;
  float panStartScrollTime_ = 0.0f;
  CameraTimelineShotEditor shotEditor_{};
  CameraTimelineKeyframeEditor keyframeEditor_{};
  CameraTimelineKeyframeClipboard keyframeClipboard_{};
  int snapFramesPerSecond_ = 30;
  bool snapEnabled_ = true;
  bool scrubbing_ = false;
  bool panning_ = false;
};

} // namespace HIKARI::EDITOR
