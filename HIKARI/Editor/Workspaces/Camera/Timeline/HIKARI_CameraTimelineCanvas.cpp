#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineCanvas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

namespace {
constexpr float kTimelineRulerHeight = 28.0f;
constexpr float kTimelineTrackHeight = 58.0f;
constexpr float kTimelineKeyframeTrackHeight = 40.0f;
constexpr float kTimelineTrackLabelWidth = 112.0f;

#if defined(HIKARI_WITH_EDITOR)
float ChooseMajorTickStep(float pixelsPerSecond) {
  if (pixelsPerSecond >= 150.0f) {
    return 0.5f;
  }
  if (pixelsPerSecond >= 70.0f) {
    return 1.0f;
  }
  if (pixelsPerSecond >= 35.0f) {
    return 2.0f;
  }
  return 5.0f;
}
#endif
} // namespace

CameraTimelineCanvasResult CameraTimelineCanvas::Draw(
    const SceneDocument &document, CinematicSequence &sequence,
    float playheadTimeSeconds, bool playing, bool editingAllowed,
    const SEQUENCER::SequenceBindingContext *previewBindings) {

  CameraTimelineCanvasResult result{};
  result.playheadTimeSeconds = playheadTimeSeconds;
#if defined(HIKARI_WITH_EDITOR)
  const ImVec2 canvasPosition = ImGui::GetCursorScreenPos();
  ImVec2 canvasSize = ImGui::GetContentRegionAvail();
  canvasSize.x = (std::max)(canvasSize.x, 320.0f);
  canvasSize.y =
      (std::max)(canvasSize.y, kTimelineRulerHeight + kTimelineTrackHeight +
                                   kTimelineKeyframeTrackHeight * 2.0f + 8.0f);
  ImGui::InvisibleButton("##CameraTimelineCanvas", canvasSize,
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonMiddle);
  const bool canvasHovered = ImGui::IsItemHovered();
  ImDrawList *drawList = ImGui::GetWindowDrawList();

  const float timelineLeft = canvasPosition.x + kTimelineTrackLabelWidth;
  const float timelineRight = canvasPosition.x + canvasSize.x;
  const float timelineWidth = (std::max)(timelineRight - timelineLeft, 1.0f);
  const float trackTop = canvasPosition.y + kTimelineRulerHeight;
  const float trackBottom = (std::min)(trackTop + kTimelineTrackHeight,
                                       canvasPosition.y + canvasSize.y);
  const float transformTrackTop = trackBottom;
  const float transformTrackBottom =
      (std::min)(transformTrackTop + kTimelineKeyframeTrackHeight,
                 canvasPosition.y + canvasSize.y);
  const float lensTrackTop = transformTrackBottom;
  const float lensTrackBottom =
      (std::min)(lensTrackTop + kTimelineKeyframeTrackHeight,
                 canvasPosition.y + canvasSize.y);

  if (canvasHovered && ImGui::GetIO().MouseWheel != 0.0f) {
    const float mouseTimelineX =
        std::clamp(ImGui::GetIO().MousePos.x, timelineLeft, timelineRight);
    const float anchorTime =
        scrollTimeSeconds_ + (mouseTimelineX - timelineLeft) / pixelsPerSecond_;
    const float zoomFactor =
        ImGui::GetIO().MouseWheel > 0.0f ? 1.15f : 1.0f / 1.15f;
    pixelsPerSecond_ = std::clamp(pixelsPerSecond_ * zoomFactor, 24.0f, 220.0f);
    scrollTimeSeconds_ =
        anchorTime - (mouseTimelineX - timelineLeft) / pixelsPerSecond_;
  }

  if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
    panning_ = true;
    panStartMouseX_ = ImGui::GetIO().MousePos.x;
    panStartScrollTime_ = scrollTimeSeconds_;
  }
  if (panning_) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
      scrollTimeSeconds_ =
          panStartScrollTime_ -
          (ImGui::GetIO().MousePos.x - panStartMouseX_) / pixelsPerSecond_;
    } else {
      panning_ = false;
    }
  }

  const float visibleDuration = timelineWidth / pixelsPerSecond_;
  const float maxScroll =
      (std::max)(sequence.durationSeconds - visibleDuration, 0.0f);
  scrollTimeSeconds_ = std::clamp(scrollTimeSeconds_, 0.0f, maxScroll);
  if (playing) {
    if (playheadTimeSeconds < scrollTimeSeconds_) {
      scrollTimeSeconds_ = playheadTimeSeconds;
    } else if (playheadTimeSeconds > scrollTimeSeconds_ + visibleDuration) {
      scrollTimeSeconds_ = playheadTimeSeconds - visibleDuration * 0.85f;
    }
    scrollTimeSeconds_ = std::clamp(scrollTimeSeconds_, 0.0f, maxScroll);
  }

  const ImU32 backgroundColor = IM_COL32(19, 23, 30, 255);
  const ImU32 rulerColor = IM_COL32(30, 36, 46, 255);
  const ImU32 labelColor = IM_COL32(25, 30, 39, 255);
  const ImU32 gridColor = IM_COL32(64, 72, 87, 150);
  const ImU32 borderColor = IM_COL32(78, 88, 105, 210);
  drawList->AddRectFilled(
      canvasPosition, ImVec2(timelineRight, canvasPosition.y + canvasSize.y),
      backgroundColor, 4.0f);
  drawList->AddRectFilled(canvasPosition, ImVec2(timelineRight, trackTop),
                          rulerColor, 4.0f, ImDrawFlags_RoundCornersTop);
  drawList->AddRectFilled(ImVec2(canvasPosition.x, trackTop),
                          ImVec2(timelineLeft, lensTrackBottom), labelColor);
  drawList->AddRect(canvasPosition,
                    ImVec2(timelineRight, canvasPosition.y + canvasSize.y),
                    borderColor, 4.0f);
  drawList->AddLine(ImVec2(timelineLeft, canvasPosition.y),
                    ImVec2(timelineLeft, canvasPosition.y + canvasSize.y),
                    borderColor);
  drawList->AddText(ImVec2(canvasPosition.x + 10.0f, trackTop + 20.0f),
                    IM_COL32(205, 211, 222, 255), "Camera Shots");
  drawList->AddText(ImVec2(canvasPosition.x + 10.0f, transformTrackTop + 12.0f),
                    IM_COL32(205, 211, 222, 255), "Transform");
  drawList->AddText(ImVec2(canvasPosition.x + 10.0f, lensTrackTop + 12.0f),
                    IM_COL32(205, 211, 222, 255), "Lens");
  drawList->AddLine(ImVec2(canvasPosition.x, transformTrackTop),
                    ImVec2(timelineRight, transformTrackTop), borderColor);
  drawList->AddLine(ImVec2(canvasPosition.x, lensTrackTop),
                    ImVec2(timelineRight, lensTrackTop), borderColor);

  const float majorStep = ChooseMajorTickStep(pixelsPerSecond_);
  const float firstMajorTime =
      std::floor(scrollTimeSeconds_ / majorStep) * majorStep;
  for (float time = firstMajorTime;
       time <= scrollTimeSeconds_ + visibleDuration + majorStep;
       time += majorStep) {
    const float x =
        timelineLeft + (time - scrollTimeSeconds_) * pixelsPerSecond_;
    if (x < timelineLeft || x > timelineRight) {
      continue;
    }
    drawList->AddLine(ImVec2(x, canvasPosition.y + 17.0f),
                      ImVec2(x, lensTrackBottom), gridColor);
    char timeLabel[32]{};
    std::snprintf(timeLabel, sizeof(timeLabel), "%.1f", time);
    drawList->AddText(ImVec2(x + 3.0f, canvasPosition.y + 3.0f),
                      IM_COL32(164, 174, 191, 255), timeLabel);
  }

  const ImVec2 mousePosition = ImGui::GetIO().MousePos;
  const CameraTimelineShotEditorResult shotResult =
      shotEditor_.Draw(document, sequence, previewBindings,
                       CameraTimelineShotLayout{
                           timelineLeft, timelineRight, trackTop, trackBottom,
                           scrollTimeSeconds_, pixelsPerSecond_, canvasHovered,
                           editingAllowed, snapEnabled_, snapFramesPerSecond_});
  result.sequenceChanged |= shotResult.sequenceChanged;

  const CameraTimelineKeyframeEditorResult keyframeResult =
      keyframeEditor_.Draw(sequence,
                           CameraTimelineKeyframeLayout{
                               timelineLeft, timelineRight, transformTrackTop,
                               transformTrackBottom, lensTrackTop,
                               lensTrackBottom, scrollTimeSeconds_,
                               pixelsPerSecond_, sequence.durationSeconds,
                               canvasHovered, editingAllowed, snapEnabled_,
                               static_cast<float>(snapFramesPerSecond_)});
  result.sequenceChanged |= keyframeResult.sequenceChanged;
  if (shotResult.selectionChanged) {
    keyframeEditor_.ClearSelection();
  }
  if (keyframeResult.selectionChanged) {
    shotEditor_.ClearSelection();
  }

  const ImGuiIO &io = ImGui::GetIO();
  const bool shortcutContext =
      editingAllowed &&
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
      !io.WantTextInput && !ImGui::IsAnyItemActive() &&
      !keyframeEditor_.IsInteractionActive();
  if (shortcutContext && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
    if (keyframeEditor_.SelectAll(sequence)) {
      shotEditor_.ClearSelection();
    }
  }
  if (shortcutContext && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
    (void)keyframeClipboard_.Copy(sequence, keyframeEditor_.GetSelections());
  }
  if (shortcutContext && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
    const CameraTimelineClipboardPasteResult paste =
        keyframeClipboard_.Paste(sequence, playheadTimeSeconds);
    if (paste.sequenceChanged) {
      keyframeEditor_.SetSelections(paste.selections);
      shotEditor_.ClearSelection();
      result.sequenceChanged = true;
    }
  }
  if (shortcutContext && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
    CameraTimelineKeyframeClipboard duplicate{};
    if (duplicate.Copy(sequence, keyframeEditor_.GetSelections())) {
      const float frameDuration =
          1.0f / static_cast<float>(snapFramesPerSecond_);
      const CameraTimelineClipboardPasteResult paste = duplicate.Paste(
          sequence, duplicate.GetSourceStartTimeSeconds() + frameDuration);
      if (paste.sequenceChanged) {
        keyframeEditor_.SetSelections(paste.selections);
        shotEditor_.ClearSelection();
        result.sequenceChanged = true;
      }
    }
  }
  if (shortcutContext && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                          ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
    result.sequenceChanged |= keyframeEditor_.DeleteSelected(sequence);
  }

  if (canvasHovered && editingAllowed &&
      ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
      !shotResult.capturedLeftClick && !keyframeResult.capturedLeftClick) {
    if (mousePosition.x >= timelineLeft) {
      scrubbing_ = true;
      result.playheadChanged = true;
      result.playheadTimeSeconds =
          std::clamp(scrollTimeSeconds_ +
                         (mousePosition.x - timelineLeft) / pixelsPerSecond_,
                     0.0f, sequence.durationSeconds);
    }
  }

  if (scrubbing_) {
    if (editingAllowed && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      result.playheadChanged = true;
      result.playheadTimeSeconds =
          std::clamp(scrollTimeSeconds_ +
                         (mousePosition.x - timelineLeft) / pixelsPerSecond_,
                     0.0f, sequence.durationSeconds);
    } else {
      scrubbing_ = false;
    }
  }

  const float playheadX =
      timelineLeft +
      (result.playheadTimeSeconds - scrollTimeSeconds_) * pixelsPerSecond_;
  if (playheadX >= timelineLeft && playheadX <= timelineRight) {
    const ImU32 playheadColor = IM_COL32(255, 111, 92, 255);
    drawList->AddLine(ImVec2(playheadX, canvasPosition.y),
                      ImVec2(playheadX, canvasPosition.y + canvasSize.y),
                      playheadColor, 2.0f);
    drawList->AddTriangleFilled(ImVec2(playheadX - 5.0f, canvasPosition.y),
                                ImVec2(playheadX + 5.0f, canvasPosition.y),
                                ImVec2(playheadX, canvasPosition.y + 7.0f),
                                playheadColor);
  }

#else
  (void)document;
  (void)sequence;
  (void)playing;
  (void)editingAllowed;
  (void)previewBindings;
#endif
  return result;
}

void CameraTimelineCanvas::Reset() {
  pixelsPerSecond_ = 90.0f;
  scrollTimeSeconds_ = 0.0f;
  snapEnabled_ = true;
  snapFramesPerSecond_ = 30;
  shotEditor_.Reset();
  keyframeEditor_.Reset();
  CancelInteraction();
}

void CameraTimelineCanvas::CancelInteraction() {
  shotEditor_.CancelInteraction();
  scrubbing_ = false;
  panning_ = false;
  keyframeEditor_.CancelInteraction();
}

uint64_t CameraTimelineCanvas::GetSelectedShotId() const noexcept {
  return shotEditor_.GetSelectedShotId();
}

void CameraTimelineCanvas::SetSelectedShotId(uint64_t shotId) noexcept {
  shotEditor_.SetSelectedShotId(shotId);
  if (shotId != 0) {
    keyframeEditor_.ClearSelection();
  }
}

float CameraTimelineCanvas::GetPixelsPerSecond() const noexcept {
  return pixelsPerSecond_;
}

void CameraTimelineCanvas::SetPixelsPerSecond(float pixelsPerSecond) noexcept {

  pixelsPerSecond_ = std::clamp(pixelsPerSecond, 24.0f, 220.0f);
}

bool CameraTimelineCanvas::IsSnapEnabled() const noexcept {
  return snapEnabled_;
}

void CameraTimelineCanvas::SetSnapEnabled(bool enabled) noexcept {
  snapEnabled_ = enabled;
}

int CameraTimelineCanvas::GetSnapFramesPerSecond() const noexcept {
  return snapFramesPerSecond_;
}

void CameraTimelineCanvas::SetSnapFramesPerSecond(
    int framesPerSecond) noexcept {

  snapFramesPerSecond_ = std::clamp(framesPerSecond, 1, 240);
}

bool CameraTimelineCanvas::HasSelectedKeyframe() const noexcept {
  return keyframeEditor_.HasSelection();
}

size_t CameraTimelineCanvas::GetSelectedKeyframeCount() const noexcept {
  return keyframeEditor_.GetSelectionCount();
}

bool CameraTimelineCanvas::DeleteSelectedKeyframe(CinematicSequence &sequence) {

  return keyframeEditor_.DeleteSelected(sequence);
}

bool CameraTimelineCanvas::DrawSelectedKeyframeInspector(
    CinematicSequence &sequence, bool editingAllowed) {

  return keyframeEditor_.DrawSelectedKeyInspector(
      sequence, editingAllowed, snapEnabled_,
      static_cast<float>(snapFramesPerSecond_));
}

void CameraTimelineCanvas::SelectTransformKeyframe(
    SEQUENCER::SequenceBindingId bindingId, uint64_t keyframeId) noexcept {

  shotEditor_.ClearSelection();
  keyframeEditor_.SelectTransformKeyframe(bindingId, keyframeId);
}

void CameraTimelineCanvas::SelectLensKeyframe(
    SEQUENCER::SequenceBindingId bindingId, uint64_t keyframeId) noexcept {

  shotEditor_.ClearSelection();
  keyframeEditor_.SelectLensKeyframe(bindingId, keyframeId);
}

} // namespace HIKARI::EDITOR
