#pragma once

#include <vector>

#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineSelection.h"
#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI::EDITOR {

class CameraTimelineKeyframeDragSession {
public:
  bool Begin(CinematicSequence &sequence,
             const std::vector<CameraTimelineKeyframeSelection> &selections,
             float mouseX);
  bool Update(CinematicSequence &sequence,
              const CameraTimelineKeyframeSelection &primarySelection,
              float mouseX, float pixelsPerSecond,
              float sequenceDurationSeconds, bool snapEnabled,
              float snapFramesPerSecond);
  void End();
  bool IsActive() const noexcept;

private:
  struct Item {
    CameraTimelineKeyframeSelection selection{};
    float startTimeSeconds = 0.0f;
  };

  std::vector<Item> items_{};
  SEQUENCER::CameraTransformTrack transformBaseline_{};
  SEQUENCER::CameraLensTrack lensBaseline_{};
  float startMouseX_ = 0.0f;
  float lastDeltaSeconds_ = 0.0f;
  bool active_ = false;
};

} // namespace HIKARI::EDITOR
