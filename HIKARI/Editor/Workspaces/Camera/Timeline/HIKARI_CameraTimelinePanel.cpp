#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelinePanel.h"

#include <algorithm>
#include <cstdio>

#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineKeyframeToolbar.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {

namespace {
constexpr float kDefaultShotDurationSeconds = 2.0f;

SEQUENCER::CameraCutClip *FindShot(CinematicSequence &sequence,
                                   uint64_t shotId) {

  const auto found =
      std::find_if(sequence.cameraCutTrack.clips.begin(),
                   sequence.cameraCutTrack.clips.end(),
                   [shotId](const SEQUENCER::CameraCutClip &shot) {
                     return shot.id == shotId;
                   });
  return found != sequence.cameraCutTrack.clips.end() ? &*found : nullptr;
}

#if defined(HIKARI_WITH_EDITOR)
bool DrawShotTransitionSettings(SEQUENCER::CameraCutClip &shot,
                                bool editingAllowed) {

  bool changed = false;
  ImGui::SameLine();
  ImGui::TextDisabled("Transition");
  ImGui::SameLine();
  if (!editingAllowed) {
    ImGui::BeginDisabled();
  }
  ImGui::SetNextItemWidth(92.0f);
  const char *transitionLabel =
      shot.transition.mode == SEQUENCER::CameraCutTransitionMode::EaseInOut
          ? "Blend"
          : "Cut";
  if (ImGui::BeginCombo("##CameraShotTransition", transitionLabel)) {
    if (ImGui::Selectable("Cut", shot.transition.mode ==
                                     SEQUENCER::CameraCutTransitionMode::Cut)) {
      shot.transition.mode = SEQUENCER::CameraCutTransitionMode::Cut;
      shot.transition.durationSeconds = 0.0f;
      changed = true;
    }
    if (ImGui::Selectable("Blend",
                          shot.transition.mode ==
                              SEQUENCER::CameraCutTransitionMode::EaseInOut)) {
      shot.transition.mode = SEQUENCER::CameraCutTransitionMode::EaseInOut;
      if (shot.transition.durationSeconds <= 0.0f) {
        shot.transition.durationSeconds = 0.5f;
      }
      changed = true;
    }
    ImGui::EndCombo();
  }
  if (shot.transition.mode == SEQUENCER::CameraCutTransitionMode::EaseInOut) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(76.0f);
    changed |=
        ImGui::DragFloat("##CameraShotBlendDuration",
                         &shot.transition.durationSeconds, 0.02f, 0.0f,
                         (std::min)(shot.durationSeconds,
                                    SEQUENCER::kMaxCameraBlendDurationSeconds),
                         "%.2f s");
  }
  if (!editingAllowed) {
    ImGui::EndDisabled();
  }
  return changed;
}
#endif
} // namespace

CameraTimelinePanelResult CameraTimelinePanel::Draw(
    SceneDocument &document, SceneCinematicsSettings &settings,
    const AssetGuid &sequenceAssetGuid, SceneObjectId selectedCameraObjectId,
    float deltaTime, bool previewAllowed, bool collectionEditingAllowed) {

  CameraTimelinePanelResult result{};
  NormalizeSceneCinematicsSettings(settings);
  if (!EnsureActiveSequence(settings)) {
    return result;
  }

#if defined(HIKARI_WITH_EDITOR)
  if (!previewAllowed) {
    PausePlayback();
  } else {
    (void)player_.Tick(settings, deltaTime,
                       SEQUENCER::SequenceEvaluationMode::Preview);
  }

  CinematicSequence *sequence =
      FindCinematicSequence(settings, activeSequenceId_);
  if (sequence == nullptr) {
    return result;
  }

  if (!previewAllowed) {
    ImGui::BeginDisabled();
  }
  if (collectionEditingAllowed) {
    ImGui::SetNextItemWidth(180.0f);
    if (ImGui::BeginCombo("##CameraSequenceSelector", sequence->name.c_str())) {
      for (const CinematicSequence &candidate : settings.sequences) {
        const bool selected = candidate.id == activeSequenceId_;
        std::string label = candidate.name;
        if (candidate.id == settings.defaultSequenceId) {
          label += "  [Default]";
        }
        if (ImGui::Selectable(label.c_str(), selected)) {
          (void)SetActiveSequence(settings, candidate.id);
        }
        if (selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::Button("New Sequence")) {
      CinematicSequence newSequence{};
      newSequence.id = AllocateCinematicSequenceId(settings);
      newSequence.name = "Sequence " + std::to_string(newSequence.id.value);
      settings.sequences.push_back(newSequence);
      (void)SetActiveSequence(settings, newSequence.id);
      result.documentChanged = true;
    }

    ImGui::SameLine();
    const bool canDeleteSequence = settings.sequences.size() > 1;
    if (!canDeleteSequence) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Delete Sequence")) {
      settings.sequences.erase(
          std::remove_if(settings.sequences.begin(), settings.sequences.end(),
                         [this](const CinematicSequence &candidate) {
                           return candidate.id == activeSequenceId_;
                         }),
          settings.sequences.end());
      NormalizeSceneCinematicsSettings(settings);
      (void)SetActiveSequence(settings, settings.defaultSequenceId);
      result.documentChanged = true;
    }
    if (!canDeleteSequence) {
      ImGui::EndDisabled();
    }
  } else {
    ImGui::TextDisabled("Asset Sequence");
  }

  sequence = FindCinematicSequence(settings, activeSequenceId_);
  if (sequence == nullptr) {
    if (!previewAllowed) {
      ImGui::EndDisabled();
    }
    return result;
  }

  if (collectionEditingAllowed) {
    ImGui::SameLine();
    const bool isDefaultSequence = sequence->id == settings.defaultSequenceId;
    if (isDefaultSequence) {
      ImGui::BeginDisabled();
    }
    if (ImGui::Button("Set Default")) {
      settings.defaultSequenceId = sequence->id;
      result.documentChanged = true;
    }
    if (isDefaultSequence) {
      ImGui::EndDisabled();
    }
  }

  ImGui::SameLine();
  ImGui::SetNextItemWidth(190.0f);
  if (ImGui::InputText("##CameraSequenceName", sequenceNameBuffer_.data(),
                       sequenceNameBuffer_.size())) {
    if (sequenceNameBuffer_[0] != '\0') {
      sequence->name = sequenceNameBuffer_.data();
      result.documentChanged = true;
    }
  }
  if (!previewAllowed) {
    ImGui::EndDisabled();
  }

  ImGui::Separator();

  const bool portableAsset = !collectionEditingAllowed;
  const SequenceBindingPanelResult bindingResult = bindingPanel_.Draw(
      document, *sequence, sequenceAssetGuid, portableAsset, previewAllowed);
  if (bindingResult.sequenceChanged) {
    NormalizeCinematicSequence(*sequence);
    result.documentChanged = true;
    previewCutPending_ = true;
  }

  ImGui::Separator();

  const bool canAddShot = previewAllowed && selectedCameraObjectId.value != 0;
  if (!canAddShot) {
    ImGui::BeginDisabled();
  }
  if (IconTextButton(EditorGlyph::Camera, "Add Shot", "CameraTimelineAddShot",
                     EditorButtonTone::Primary, ImVec2(0.0f, 28.0f),
                     canAddShot ? "Add the selected camera at the playhead"
                                : "Select a Camera object to add a shot")) {
    SEQUENCER::CameraCutClip shot{};
    shot.id = SEQUENCER::AllocateCameraCutClipId(sequence->cameraCutTrack);
    shot.cameraBindingId =
        portableAsset ? FindOrCreatePortableCameraBinding(
                            document, *sequence, selectedCameraObjectId,
                            bindingPanel_.GetPreviewBindings())
                      : SEQUENCER::FindOrCreateSceneObjectBinding(
                            sequence->bindings, selectedCameraObjectId);
    shot.startTimeSeconds = player_.GetTimeSeconds();
    shot.durationSeconds = kDefaultShotDurationSeconds;
    sequence->cameraCutTrack.clips.push_back(shot);
    sequence->durationSeconds =
        (std::max)(sequence->durationSeconds,
                   shot.startTimeSeconds + shot.durationSeconds);
    NormalizeCinematicSequence(*sequence);
    canvas_.SetSelectedShotId(shot.id);
    previewEnabled_ = true;
    previewCutPending_ = true;
    result.documentChanged = true;
  }
  if (!canAddShot) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  if (!previewAllowed) {
    ImGui::BeginDisabled();
  }
  if (IconButton(player_.IsPlaying() ? EditorGlyph::Pause : EditorGlyph::Play,
                 "CameraTimelinePlayback",
                 player_.IsPlaying() ? EditorButtonTone::Primary
                                     : EditorButtonTone::Neutral,
                 ImVec2(28.0f, 28.0f),
                 player_.IsPlaying() ? "Pause preview" : "Play preview")) {
    if (player_.IsPlaying()) {
      player_.Pause();
    } else {
      if (player_.GetTimeSeconds() >= sequence->durationSeconds) {
        (void)player_.Seek(settings, 0.0f);
      }
      (void)player_.Play();
    }
    previewEnabled_ = true;
  }
  ImGui::SameLine();
  if (IconButton(EditorGlyph::Stop, "CameraTimelineStop",
                 EditorButtonTone::Quiet, ImVec2(28.0f, 28.0f),
                 "Stop preview and return to the sequence start")) {
    player_.Stop();
    previewEnabled_ = true;
    previewCutPending_ = true;
  }
  if (!previewAllowed) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  ImGui::SetNextItemWidth(82.0f);
  float currentTimeSeconds = player_.GetTimeSeconds();
  if (ImGui::DragFloat("##CameraTimelineTime", &currentTimeSeconds, 0.02f, 0.0f,
                       sequence->durationSeconds, "%.2f s")) {
    player_.Pause();
    (void)player_.Seek(settings, currentTimeSeconds);
    previewEnabled_ = true;
    previewCutPending_ = true;
  }

  ImGui::SameLine();
  bool previewEnabled = previewEnabled_;
  if (ImGui::Checkbox("Preview", &previewEnabled)) {
    previewEnabled_ = previewEnabled;
  }

  ImGui::SameLine();
  const bool canDeleteSelectedShot =
      previewAllowed &&
      FindShot(*sequence, canvas_.GetSelectedShotId()) != nullptr;
  if (!canDeleteSelectedShot) {
    ImGui::BeginDisabled();
  }
  if (IconButton(EditorGlyph::Delete, "CameraTimelineDeleteShot",
                 EditorButtonTone::Danger, ImVec2(28.0f, 28.0f),
                 "Delete the selected camera shot")) {
    const uint64_t selectedShotId = canvas_.GetSelectedShotId();
    sequence->cameraCutTrack.clips.erase(
        std::remove_if(sequence->cameraCutTrack.clips.begin(),
                       sequence->cameraCutTrack.clips.end(),
                       [selectedShotId](const SEQUENCER::CameraCutClip &shot) {
                         return shot.id == selectedShotId;
                       }),
        sequence->cameraCutTrack.clips.end());
    canvas_.SetSelectedShotId(0);
    previewCutPending_ = true;
    result.documentChanged = true;
  }
  if (!canDeleteSelectedShot) {
    ImGui::EndDisabled();
  }

  if (SEQUENCER::CameraCutClip *selectedShot =
          FindShot(*sequence, canvas_.GetSelectedShotId())) {
    if (DrawShotTransitionSettings(*selectedShot, previewAllowed)) {
      NormalizeCinematicSequence(*sequence);
      result.documentChanged = true;
    }
  }

  ImGui::SameLine();
  ImGui::TextDisabled("Duration");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(78.0f);
  float sequenceDuration = sequence->durationSeconds;
  if (!previewAllowed) {
    ImGui::BeginDisabled();
  }
  if (ImGui::DragFloat("##CameraTimelineDuration", &sequenceDuration, 0.1f,
                       kMinCinematicSequenceDurationSeconds,
                       kMaxCinematicSequenceDurationSeconds, "%.1f s")) {
    sequence->durationSeconds = sequenceDuration;
    NormalizeCinematicSequence(*sequence);
    (void)player_.Seek(settings, player_.GetTimeSeconds());
    result.documentChanged = true;
  }
  if (!previewAllowed) {
    ImGui::EndDisabled();
  }

  ImGui::SameLine();
  ImGui::TextDisabled("Zoom");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100.0f);
  float pixelsPerSecond = canvas_.GetPixelsPerSecond();
  if (ImGui::SliderFloat("##CameraTimelineZoom", &pixelsPerSecond, 24.0f,
                         220.0f, "%.0f px/s")) {
    canvas_.SetPixelsPerSecond(pixelsPerSecond);
  }

  const CameraTimelineKeyframeToolbarResult keyframeToolbar =
      DrawCameraTimelineKeyframeToolbar(
          document, *sequence, selectedCameraObjectId, player_.GetTimeSeconds(),
          previewAllowed, portableAsset, &bindingPanel_.GetPreviewBindings(),
          canvas_);
  if (keyframeToolbar.sequenceChanged) {
    NormalizeCinematicSequence(*sequence);
    (void)player_.Seek(settings, player_.GetTimeSeconds());
    result.documentChanged = true;
  }
  if (keyframeToolbar.previewRequested) {
    previewEnabled_ = true;
  }

  ImGui::Separator();
  const CameraTimelineCanvasResult canvasResult = canvas_.Draw(
      document, *sequence, player_.GetTimeSeconds(), player_.IsPlaying(),
      previewAllowed, &bindingPanel_.GetPreviewBindings());
  if (canvasResult.playheadChanged) {
    player_.Pause();
    (void)player_.Seek(settings, canvasResult.playheadTimeSeconds);
    previewEnabled_ = true;
    previewCutPending_ = true;
  }
  if (canvasResult.sequenceChanged) {
    NormalizeCinematicSequence(*sequence);
    (void)player_.Seek(settings, player_.GetTimeSeconds());
    result.documentChanged = true;
  }

  result.previewEnabled = previewAllowed && previewEnabled_;
  result.editMergeId = static_cast<uint64_t>(ImGui::GetActiveID());
  result.forceCameraCut = previewCutPending_ ||
                          player_.GetLastEvaluationContext().IsDiscontinuous();
  result.sequenceId = activeSequenceId_;
  if (result.previewEnabled) {
    result.evaluation =
        EvaluateCinematicCameraTrack(*sequence, player_.GetTimeSeconds(),
                                     bindingPanel_.GetPreviewBindings());
  }
  previewCutPending_ = false;
#else
  (void)document;
  (void)sequenceAssetGuid;
  (void)selectedCameraObjectId;
  (void)deltaTime;
  (void)previewAllowed;
  (void)collectionEditingAllowed;
#endif
  return result;
}

void CameraTimelinePanel::ResetForScene() {
  player_.Clear();
  bindingPanel_.Reset();
  canvas_.Reset();
  activeSequenceId_ = {};
  sequenceNameBuffer_.fill('\0');
  previewEnabled_ = true;
  previewCutPending_ = true;
}

void CameraTimelinePanel::PausePlayback() {
  player_.Pause();
  canvas_.CancelInteraction();
}

void CameraTimelinePanel::SuspendPreview() {
  PausePlayback();
  previewEnabled_ = false;
  previewCutPending_ = true;
}

CinematicSequence *
CameraTimelinePanel::GetActiveSequence(SceneCinematicsSettings &settings) {

  NormalizeSceneCinematicsSettings(settings);
  return EnsureActiveSequence(settings)
             ? FindCinematicSequence(settings, activeSequenceId_)
             : nullptr;
}

bool CameraTimelinePanel::EnsureActiveSequence(
    SceneCinematicsSettings &settings) {

  CinematicSequence *sequence =
      FindCinematicSequence(settings, activeSequenceId_);
  if (sequence == nullptr) {
    return SetActiveSequence(settings, settings.defaultSequenceId);
  }
  if (!(player_.GetSequenceId() == activeSequenceId_)) {
    return player_.Bind(settings, activeSequenceId_, 0.0f);
  }
  return true;
}

bool CameraTimelinePanel::SetActiveSequence(SceneCinematicsSettings &settings,
                                            CinematicSequenceId sequenceId) {

  CinematicSequence *sequence = FindCinematicSequence(settings, sequenceId);
  if (sequence == nullptr || !player_.Bind(settings, sequenceId, 0.0f)) {
    return false;
  }
  activeSequenceId_ = sequenceId;
  canvas_.Reset();
  SyncNameBuffer(*sequence);
  previewEnabled_ = true;
  previewCutPending_ = true;
  return true;
}

void CameraTimelinePanel::SyncNameBuffer(const CinematicSequence &sequence) {

  sequenceNameBuffer_.fill('\0');
  std::snprintf(sequenceNameBuffer_.data(), sequenceNameBuffer_.size(), "%s",
                sequence.name.c_str());
}

} // namespace HIKARI::EDITOR
