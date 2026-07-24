#pragma once

#include <string>
#include <vector>

#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineSelection.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI::EDITOR {

struct CameraTimelineClipboardPasteResult {
  std::vector<CameraTimelineKeyframeSelection> selections{};
  bool sequenceChanged = false;
};

class CameraTimelineKeyframeClipboard {
public:
  bool Copy(const CinematicSequence &sequence,
            const std::vector<CameraTimelineKeyframeSelection> &selections);
  CameraTimelineClipboardPasteResult Paste(CinematicSequence &sequence,
                                           float anchorTimeSeconds) const;

  bool Empty() const noexcept;
  size_t Size() const noexcept;
  float GetSourceStartTimeSeconds() const noexcept;

private:
  struct TransformPayload {
    SEQUENCER::SequenceBindingTargetKind bindingTargetKind =
        SEQUENCER::SequenceBindingTargetKind::SceneObject;
    SceneObjectId cameraObjectId{};
    std::string bindingName{};
    std::string slotName{};
    float timeOffsetSeconds = 0.0f;
    MATH::Vec3 position{};
    MATH::Vec3 rotationEulerDeg{};
    SEQUENCER::SequenceInterpolationMode interpolation =
        SEQUENCER::SequenceInterpolationMode::Smooth;
  };

  struct LensPayload {
    SEQUENCER::SequenceBindingTargetKind bindingTargetKind =
        SEQUENCER::SequenceBindingTargetKind::SceneObject;
    SceneObjectId cameraObjectId{};
    std::string bindingName{};
    std::string slotName{};
    float timeOffsetSeconds = 0.0f;
    float verticalFovDegrees = 60.0f;
    float nearClip = 0.1f;
    float farClip = 100.0f;
    SEQUENCER::SequenceInterpolationMode interpolation =
        SEQUENCER::SequenceInterpolationMode::Smooth;
  };

  std::vector<TransformPayload> transforms_{};
  std::vector<LensPayload> lenses_{};
  float sourceStartTimeSeconds_ = 0.0f;
};

} // namespace HIKARI::EDITOR
