#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineKeyframeAuthoring.h"

#include <algorithm>
#include <cmath>

#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::EDITOR {

namespace {
const SceneObjectData *FindSceneObject(const SceneDocument &document,
                                       SceneObjectId objectId) noexcept {

  const auto found =
      std::find_if(document.objects.begin(), document.objects.end(),
                   [objectId](const SceneObjectData &object) {
                     return object.id == objectId;
                   });
  return found != document.objects.end() ? &*found : nullptr;
}

struct CameraLensValues {
  float verticalFovDegrees = 60.0f;
  float nearClip = 0.1f;
  float farClip = 100.0f;
};

bool TryGetCameraLensValues(const SceneObjectData &object,
                            CameraLensValues &outValues) {

  const auto found =
      std::find_if(object.components.begin(), object.components.end(),
                   [](const SceneComponentData &component) {
                     return component.type == "CameraComponent";
                   });
  if (found == object.components.end()) {
    return false;
  }
  outValues.verticalFovDegrees =
      found->properties.value("verticalFovDegrees", 60.0f);
  outValues.nearClip = found->properties.value("nearClip", 0.1f);
  outValues.farClip = found->properties.value("farClip", 100.0f);
  return std::isfinite(outValues.verticalFovDegrees) &&
         std::isfinite(outValues.nearClip) && std::isfinite(outValues.farClip);
}

SEQUENCER::SequenceBindingId
ResolveCaptureBinding(CinematicSequence &sequence, SceneObjectId cameraObjectId,
                      const std::string &cameraName,
                      SEQUENCER::SequenceBindingId preferredBindingId) {

  if (preferredBindingId.IsValid() &&
      SEQUENCER::FindSequenceBinding(sequence.bindings, preferredBindingId) !=
          nullptr) {
    return preferredBindingId;
  }
  return SEQUENCER::FindOrCreateSceneObjectBinding(sequence.bindings,
                                                   cameraObjectId, cameraName);
}
} // namespace

CameraKeyframeCaptureResult CaptureCameraTransformKeyframe(
    SceneDocument &document, CinematicSequence &sequence,
    SceneObjectId cameraObjectId, float timeSeconds,
    SEQUENCER::SequenceBindingId preferredBindingId) {

  CameraKeyframeCaptureResult result{};
  const SceneObjectData *cameraObject =
      FindSceneObject(document, cameraObjectId);
  CameraLensValues cameraLens{};
  if (cameraObject == nullptr || !std::isfinite(timeSeconds) ||
      !MATH::IsFinite(cameraObject->transform.position) ||
      !MATH::IsFinite(cameraObject->transform.rotationEulerDeg) ||
      !TryGetCameraLensValues(*cameraObject, cameraLens)) {
    return result;
  }
  result.cameraBindingId = ResolveCaptureBinding(
      sequence, cameraObjectId, cameraObject->name, preferredBindingId);
  result.transformKeyframeId = SEQUENCER::SetCameraTransformKeyframe(
      sequence.cameraTransformTrack, result.cameraBindingId, timeSeconds,
      cameraObject->transform.position,
      cameraObject->transform.rotationEulerDeg);
  if (!result.Succeeded()) {
    return {};
  }
  sequence.durationSeconds = (std::max)(sequence.durationSeconds, timeSeconds);
  NormalizeCinematicSequence(sequence);
  return result;
}

CameraKeyframeCaptureResult
CaptureCameraLensKeyframe(SceneDocument &document, CinematicSequence &sequence,
                          SceneObjectId cameraObjectId, float timeSeconds,
                          SEQUENCER::SequenceBindingId preferredBindingId) {

  CameraKeyframeCaptureResult result{};
  const SceneObjectData *cameraObject =
      FindSceneObject(document, cameraObjectId);
  CameraLensValues lens{};
  if (cameraObject == nullptr || !std::isfinite(timeSeconds) ||
      !TryGetCameraLensValues(*cameraObject, lens)) {
    return result;
  }
  result.cameraBindingId = ResolveCaptureBinding(
      sequence, cameraObjectId, cameraObject->name, preferredBindingId);
  result.lensKeyframeId = SEQUENCER::SetCameraLensKeyframe(
      sequence.cameraLensTrack, result.cameraBindingId, timeSeconds,
      lens.verticalFovDegrees, lens.nearClip, lens.farClip);
  if (!result.Succeeded()) {
    return {};
  }
  sequence.durationSeconds = (std::max)(sequence.durationSeconds, timeSeconds);
  NormalizeCinematicSequence(sequence);
  return result;
}

CameraKeyframeCaptureResult
CaptureCameraKeyframe(SceneDocument &document, CinematicSequence &sequence,
                      SceneObjectId cameraObjectId, float timeSeconds,
                      SEQUENCER::SequenceBindingId preferredBindingId) {

  CameraKeyframeCaptureResult result{};
  const SceneObjectData *cameraObject =
      FindSceneObject(document, cameraObjectId);
  CameraLensValues lens{};
  if (cameraObject == nullptr || !std::isfinite(timeSeconds) ||
      !MATH::IsFinite(cameraObject->transform.position) ||
      !MATH::IsFinite(cameraObject->transform.rotationEulerDeg) ||
      !TryGetCameraLensValues(*cameraObject, lens)) {
    return result;
  }

  result.cameraBindingId = ResolveCaptureBinding(
      sequence, cameraObjectId, cameraObject->name, preferredBindingId);
  if (!result.cameraBindingId.IsValid()) {
    return {};
  }
  result.transformKeyframeId = SEQUENCER::SetCameraTransformKeyframe(
      sequence.cameraTransformTrack, result.cameraBindingId, timeSeconds,
      cameraObject->transform.position,
      cameraObject->transform.rotationEulerDeg);
  result.lensKeyframeId = SEQUENCER::SetCameraLensKeyframe(
      sequence.cameraLensTrack, result.cameraBindingId, timeSeconds,
      lens.verticalFovDegrees, lens.nearClip, lens.farClip);
  if (!result.CapturedBoth()) {
    return {};
  }
  sequence.durationSeconds = (std::max)(sequence.durationSeconds, timeSeconds);
  NormalizeCinematicSequence(sequence);
  return result;
}

} // namespace HIKARI::EDITOR
