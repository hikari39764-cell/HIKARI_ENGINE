#pragma once

#include <cstdint>

#include "Scene/HIKARI_CinematicSequence.h"
#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI::EDITOR {

struct CameraKeyframeCaptureResult {
  SEQUENCER::SequenceBindingId cameraBindingId{};
  uint64_t transformKeyframeId = 0;
  uint64_t lensKeyframeId = 0;

  bool Succeeded() const noexcept {
    return cameraBindingId.IsValid() &&
           (transformKeyframeId != 0 || lensKeyframeId != 0);
  }

  bool CapturedBoth() const noexcept {
    return cameraBindingId.IsValid() && transformKeyframeId != 0 &&
           lensKeyframeId != 0;
  }
};

CameraKeyframeCaptureResult CaptureCameraTransformKeyframe(
    SceneDocument &document, CinematicSequence &sequence,
    SceneObjectId cameraObjectId, float timeSeconds,
    SEQUENCER::SequenceBindingId preferredBindingId = {});
CameraKeyframeCaptureResult
CaptureCameraLensKeyframe(SceneDocument &document, CinematicSequence &sequence,
                          SceneObjectId cameraObjectId, float timeSeconds,
                          SEQUENCER::SequenceBindingId preferredBindingId = {});
CameraKeyframeCaptureResult
CaptureCameraKeyframe(SceneDocument &document, CinematicSequence &sequence,
                      SceneObjectId cameraObjectId, float timeSeconds,
                      SEQUENCER::SequenceBindingId preferredBindingId = {});

} // namespace HIKARI::EDITOR
