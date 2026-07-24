#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineKeyframeToolbar.h"

#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineKeyframeAuthoring.h"
#include "Editor/Workspaces/Sequence/HIKARI_SequenceBindingPanel.h"

#include <string>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

CameraTimelineKeyframeToolbarResult DrawCameraTimelineKeyframeToolbar(
    SceneDocument &document, CinematicSequence &sequence,
    SceneObjectId selectedCameraObjectId, float playheadTimeSeconds,
    bool editingAllowed, bool portableAsset,
    SEQUENCER::SequenceBindingContext *previewBindings,
    CameraTimelineCanvas &canvas) {

  CameraTimelineKeyframeToolbarResult result{};
#if defined(HIKARI_WITH_EDITOR)
  const bool canCapture = editingAllowed && selectedCameraObjectId.value != 0;
  const auto resolveCaptureBinding = [&]() {
    return portableAsset && previewBindings != nullptr
               ? FindOrCreatePortableCameraBinding(document, sequence,
                                                   selectedCameraObjectId,
                                                   *previewBindings)
               : SEQUENCER::SequenceBindingId{};
  };
  if (!canCapture) {
    ImGui::BeginDisabled();
  }
  if (IconTextButton(EditorGlyph::Transform, "Transform Key",
                     "CameraTimelineAddTransformKey", EditorButtonTone::Neutral,
                     ImVec2(0.0f, 28.0f),
                     "Capture camera transform at the playhead")) {
    const CameraKeyframeCaptureResult capture = CaptureCameraTransformKeyframe(
        document, sequence, selectedCameraObjectId, playheadTimeSeconds,
        resolveCaptureBinding());
    if (capture.Succeeded()) {
      canvas.SelectTransformKeyframe(capture.cameraBindingId,
                                     capture.transformKeyframeId);
      result.sequenceChanged = true;
      result.previewRequested = true;
    }
  }
  ImGui::SameLine();
  if (IconTextButton(EditorGlyph::Lens, "Lens Key", "CameraTimelineAddLensKey",
                     EditorButtonTone::Neutral, ImVec2(0.0f, 28.0f),
                     "Capture camera lens settings at the playhead")) {
    const CameraKeyframeCaptureResult capture =
        CaptureCameraLensKeyframe(document, sequence, selectedCameraObjectId,
                                  playheadTimeSeconds, resolveCaptureBinding());
    if (capture.Succeeded()) {
      canvas.SelectLensKeyframe(capture.cameraBindingId,
                                capture.lensKeyframeId);
      result.sequenceChanged = true;
      result.previewRequested = true;
    }
  }
  ImGui::SameLine();
  if (IconTextButton(
          EditorGlyph::Camera, "Camera Key", "CameraTimelineAddCameraKey",
          EditorButtonTone::Primary, ImVec2(0.0f, 28.0f),
          canCapture ? "Capture transform and lens at the playhead"
                     : "Select a Camera object to capture a keyframe")) {
    const CameraKeyframeCaptureResult capture =
        CaptureCameraKeyframe(document, sequence, selectedCameraObjectId,
                              playheadTimeSeconds, resolveCaptureBinding());
    if (capture.CapturedBoth()) {
      canvas.SelectTransformKeyframe(capture.cameraBindingId,
                                     capture.transformKeyframeId);
      result.sequenceChanged = true;
      result.previewRequested = true;
    }
  }
  if (!canCapture) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  const bool canDelete = editingAllowed && canvas.HasSelectedKeyframe();
  if (!canDelete) {
    ImGui::BeginDisabled();
  }
  const size_t selectedKeyCount = canvas.GetSelectedKeyframeCount();
  const std::string deleteLabel =
      selectedKeyCount > 1
          ? "Delete Keys (" + std::to_string(selectedKeyCount) + ")"
          : "Delete Key";
  if (IconTextButton(EditorGlyph::Delete, deleteLabel.c_str(),
                     "CameraTimelineDeleteKeyframes", EditorButtonTone::Danger,
                     ImVec2(0.0f, 28.0f),
                     "Delete the selected keyframe or keyframes")) {
    const bool deleted = canvas.DeleteSelectedKeyframe(sequence);
    result.sequenceChanged |= deleted;
    result.previewRequested |= deleted;
  }
  if (!canDelete) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (!editingAllowed) {
    ImGui::BeginDisabled();
  }
  bool transformEnabled = sequence.cameraTransformTrack.enabled;
  if (IconToggleButton(EditorGlyph::Transform, "CameraTimelineTransformTrack",
                       transformEnabled, ImVec2(28.0f, 28.0f),
                       transformEnabled ? "Disable camera transform track"
                                        : "Enable camera transform track")) {
    sequence.cameraTransformTrack.enabled = !transformEnabled;
    result.sequenceChanged = true;
    result.previewRequested = true;
  }
  ImGui::SameLine();
  bool lensEnabled = sequence.cameraLensTrack.enabled;
  if (IconToggleButton(EditorGlyph::Lens, "CameraTimelineLensTrack",
                       lensEnabled, ImVec2(28.0f, 28.0f),
                       lensEnabled ? "Disable camera lens track"
                                   : "Enable camera lens track")) {
    sequence.cameraLensTrack.enabled = !lensEnabled;
    result.sequenceChanged = true;
    result.previewRequested = true;
  }
  if (!editingAllowed) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  bool snapEnabled = canvas.IsSnapEnabled();
  if (IconToggleButton(EditorGlyph::Snap, "CameraTimelineSnap", snapEnabled,
                       ImVec2(28.0f, 28.0f),
                       snapEnabled ? "Disable timeline snapping"
                                   : "Enable timeline snapping")) {
    canvas.SetSnapEnabled(!snapEnabled);
  }
  ImGui::SameLine();
  int snapFramesPerSecond = canvas.GetSnapFramesPerSecond();
  ImGui::SetNextItemWidth(82.0f);
  if (ImGui::DragInt("##CameraTimelineSnapRate", &snapFramesPerSecond, 1.0f, 1,
                     240, "%d fps")) {
    canvas.SetSnapFramesPerSecond(snapFramesPerSecond);
  }

  ImGui::SameLine();
  if (IconButton(EditorGlyph::More, "CameraTimelineControls",
                 EditorButtonTone::Quiet, ImVec2(28.0f, 28.0f),
                 "Timeline controls")) {
    ImGui::OpenPopup("CameraTimelineControlsPopup");
  }
  if (ImGui::BeginPopup("CameraTimelineControlsPopup")) {
    ImGui::TextUnformatted("Timeline controls");
    ImGui::Separator();
    ImGui::TextDisabled("Shift + click      Add to selection");
    ImGui::TextDisabled("Ctrl + click       Toggle selection");
    ImGui::TextDisabled("Drag empty lane    Box select");
    ImGui::TextDisabled("Ctrl + A/C/V/D     Select/copy/paste/duplicate");
    ImGui::TextDisabled("Delete             Remove selected keys");
    ImGui::EndPopup();
  }

  if (canvas.DrawSelectedKeyframeInspector(sequence, editingAllowed)) {
    result.sequenceChanged = true;
    result.previewRequested = true;
  }

#else
  (void)document;
  (void)sequence;
  (void)selectedCameraObjectId;
  (void)playheadTimeSeconds;
  (void)editingAllowed;
  (void)portableAsset;
  (void)previewBindings;
  (void)canvas;
#endif
  return result;
}

} // namespace HIKARI::EDITOR
