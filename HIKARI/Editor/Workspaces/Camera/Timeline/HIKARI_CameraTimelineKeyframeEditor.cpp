#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineKeyframeEditor.h"

#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineKeyframeInspector.h"

#include <algorithm>
#include <limits>
#include <vector>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

namespace {
#if defined(HIKARI_WITH_EDITOR)
constexpr float kKeyframeRadius = 6.0f;
constexpr float kKeyframeHitRadius = 9.0f;

struct VisibleKeyframe {
  CameraTimelineKeyframeSelection selection{};
  float timeSeconds = 0.0f;
  float x = 0.0f;
  float y = 0.0f;
};

ImU32 KeyframeColor(SEQUENCER::SequenceBindingId bindingId, bool selected) {

  const uint32_t hash =
      static_cast<uint32_t>(bindingId.value ^ (bindingId.value >> 32u));
  const float hue = static_cast<float>(hash % 360u) / 360.0f;
  ImVec4 color{};
  ImGui::ColorConvertHSVtoRGB(hue, selected ? 0.52f : 0.45f,
                              selected ? 1.0f : 0.88f, color.x, color.y,
                              color.z);
  color.w = 1.0f;
  return ImGui::ColorConvertFloat4ToU32(color);
}

void DrawDiamond(ImDrawList &drawList, float x, float y, ImU32 fill,
                 bool selected) {

  const ImVec2 points[4] = {{x, y - kKeyframeRadius},
                            {x + kKeyframeRadius, y},
                            {x, y + kKeyframeRadius},
                            {x - kKeyframeRadius, y}};
  drawList.AddConvexPolyFilled(points, 4, fill);
  drawList.AddPolyline(points, 4,
                       selected ? IM_COL32(255, 230, 142, 255)
                                : IM_COL32(220, 226, 236, 230),
                       ImDrawFlags_Closed, selected ? 2.0f : 1.0f);
}

CameraTimelineSelectionOperation SelectionOperationFromInput() {
  const ImGuiIO &io = ImGui::GetIO();
  if (io.KeyCtrl) {
    return CameraTimelineSelectionOperation::Toggle;
  }
  if (io.KeyShift) {
    return CameraTimelineSelectionOperation::Add;
  }
  return CameraTimelineSelectionOperation::Replace;
}

bool FindSelectionTime(CinematicSequence &sequence,
                       const CameraTimelineKeyframeSelection &selection,
                       float &outTimeSeconds) {

  if (selection.kind == CameraTimelineKeyframeKind::Transform) {
    const SEQUENCER::CameraTransformKeyframe *keyframe =
        SEQUENCER::FindCameraTransformKeyframe(sequence.cameraTransformTrack,
                                               selection.bindingId,
                                               selection.keyframeId);
    if (keyframe != nullptr) {
      outTimeSeconds = keyframe->timeSeconds;
      return true;
    }
  } else if (selection.kind == CameraTimelineKeyframeKind::Lens) {
    const SEQUENCER::CameraLensKeyframe *keyframe =
        SEQUENCER::FindCameraLensKeyframe(sequence.cameraLensTrack,
                                          selection.bindingId,
                                          selection.keyframeId);
    if (keyframe != nullptr) {
      outTimeSeconds = keyframe->timeSeconds;
      return true;
    }
  }
  return false;
}

#endif
} // namespace

CameraTimelineKeyframeEditorResult
CameraTimelineKeyframeEditor::Draw(CinematicSequence &sequence,
                                   const CameraTimelineKeyframeLayout &layout) {

  CameraTimelineKeyframeEditorResult result{};
#if defined(HIKARI_WITH_EDITOR)
  std::vector<CameraTimelineKeyframeSelection> validSelections{};
  validSelections.reserve(selection_.Size());
  for (const CameraTimelineKeyframeSelection &selection : selection_.Items()) {
    float ignoredTime = 0.0f;
    if (FindSelectionTime(sequence, selection, ignoredTime)) {
      validSelections.push_back(selection);
    }
  }
  result.selectionChanged |= selection_.Apply(
      validSelections, CameraTimelineSelectionOperation::Replace);

  ImDrawList &drawList = *ImGui::GetWindowDrawList();
  const ImVec2 mousePosition = ImGui::GetIO().MousePos;
  std::vector<VisibleKeyframe> visibleKeyframes{};
  size_t keyframeCount = 0;
  for (const auto &channel : sequence.cameraTransformTrack.channels) {
    keyframeCount += channel.keyframes.size();
  }
  for (const auto &channel : sequence.cameraLensTrack.channels) {
    keyframeCount += channel.keyframes.size();
  }
  visibleKeyframes.reserve(keyframeCount);
  const VisibleKeyframe *hovered = nullptr;
  float hoveredDistanceSquared = (std::numeric_limits<float>::max)();

  const auto drawKeyframe = [&](CameraTimelineKeyframeKind kind,
                                SEQUENCER::SequenceBindingId bindingId,
                                uint64_t keyframeId, float timeSeconds,
                                float laneTop, float laneBottom) {
    const float x =
        layout.timelineLeft +
        (timeSeconds - layout.scrollTimeSeconds) * layout.pixelsPerSecond;
    if (x < layout.timelineLeft || x > layout.timelineRight) {
      return;
    }
    const float y = (laneTop + laneBottom) * 0.5f;
    visibleKeyframes.push_back(
        {{kind, bindingId, keyframeId}, timeSeconds, x, y});
    const VisibleKeyframe &visible = visibleKeyframes.back();
    const bool selected = selection_.Contains(visible.selection);
    DrawDiamond(drawList, x, y, KeyframeColor(bindingId, selected), selected);
    const float deltaX = mousePosition.x - x;
    const float deltaY = mousePosition.y - y;
    const float distanceSquared = deltaX * deltaX + deltaY * deltaY;
    if (distanceSquared <= kKeyframeHitRadius * kKeyframeHitRadius &&
        distanceSquared < hoveredDistanceSquared) {
      hovered = &visible;
      hoveredDistanceSquared = distanceSquared;
    }
  };

  for (const SEQUENCER::CameraTransformChannel &channel :
       sequence.cameraTransformTrack.channels) {
    for (const SEQUENCER::CameraTransformKeyframe &keyframe :
         channel.keyframes) {
      drawKeyframe(CameraTimelineKeyframeKind::Transform,
                   channel.cameraBindingId, keyframe.id, keyframe.timeSeconds,
                   layout.transformTop, layout.transformBottom);
    }
  }
  for (const SEQUENCER::CameraLensChannel &channel :
       sequence.cameraLensTrack.channels) {
    for (const SEQUENCER::CameraLensKeyframe &keyframe : channel.keyframes) {
      drawKeyframe(CameraTimelineKeyframeKind::Lens, channel.cameraBindingId,
                   keyframe.id, keyframe.timeSeconds, layout.lensTop,
                   layout.lensBottom);
    }
  }

  if (layout.canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    if (hovered != nullptr) {
      const CameraTimelineSelectionOperation operation =
          SelectionOperationFromInput();
      if (operation == CameraTimelineSelectionOperation::Replace &&
          selection_.Contains(hovered->selection)) {
        // Preserve the current set so dragging any selected key
        // moves the whole selection.
      } else {
        result.selectionChanged |=
            selection_.Apply(hovered->selection, operation);
      }
      result.capturedLeftClick = true;
      if (layout.editingAllowed && selection_.Contains(hovered->selection)) {
        (void)dragSession_.Begin(sequence, selection_.Items(), mousePosition.x);
      }
    } else {
      const bool insideKeyframeTracks =
          mousePosition.x >= layout.timelineLeft &&
          mousePosition.x <= layout.timelineRight &&
          mousePosition.y >= layout.transformTop &&
          mousePosition.y <= layout.lensBottom;
      if (insideKeyframeTracks) {
        marqueeActive_ = true;
        marqueeStartX_ = mousePosition.x;
        marqueeStartY_ = mousePosition.y;
        marqueeOperation_ = SelectionOperationFromInput();
        result.capturedLeftClick = true;
      }
    }
  }

  if (dragSession_.IsActive()) {
    if (layout.editingAllowed && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      result.sequenceChanged |= dragSession_.Update(
          sequence, selection_.Primary(), mousePosition.x,
          layout.pixelsPerSecond, layout.sequenceDurationSeconds,
          layout.snapEnabled, layout.snapFramesPerSecond);
    } else {
      dragSession_.End();
    }
  }

  if (marqueeActive_) {
    const ImVec2 minimum{(std::min)(marqueeStartX_, mousePosition.x),
                         (std::min)(marqueeStartY_, mousePosition.y)};
    const ImVec2 maximum{(std::max)(marqueeStartX_, mousePosition.x),
                         (std::max)(marqueeStartY_, mousePosition.y)};
    drawList.AddRectFilled(minimum, maximum, IM_COL32(76, 139, 211, 42));
    drawList.AddRect(minimum, maximum, IM_COL32(105, 173, 245, 220), 0.0f, 0,
                     1.5f);
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      std::vector<CameraTimelineKeyframeSelection> enclosed{};
      for (const VisibleKeyframe &visible : visibleKeyframes) {
        if (visible.x >= minimum.x && visible.x <= maximum.x &&
            visible.y >= minimum.y && visible.y <= maximum.y) {
          enclosed.push_back(visible.selection);
        }
      }
      result.selectionChanged |= selection_.Apply(enclosed, marqueeOperation_);
      marqueeActive_ = false;
    }
  }

  if (layout.canvasHovered && hovered != nullptr && !marqueeActive_) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    ImGui::SetTooltip(
        "%s Key\nTime %.3f s%s",
        hovered->selection.kind == CameraTimelineKeyframeKind::Transform
            ? "Transform"
            : "Lens",
        hovered->timeSeconds,
        selection_.Contains(hovered->selection) ? "\nSelected" : "");
  }
#else
  (void)sequence;
  (void)layout;
#endif
  return result;
}

void CameraTimelineKeyframeEditor::Reset() {
  ClearSelection();
  CancelInteraction();
}

void CameraTimelineKeyframeEditor::CancelInteraction() {
  dragSession_.End();
  marqueeActive_ = false;
}

void CameraTimelineKeyframeEditor::ClearSelection() {
  (void)selection_.Clear();
}

bool CameraTimelineKeyframeEditor::HasSelection() const noexcept {
  return !selection_.Empty();
}

size_t CameraTimelineKeyframeEditor::GetSelectionCount() const noexcept {
  return selection_.Size();
}

CameraTimelineKeyframeSelection
CameraTimelineKeyframeEditor::GetSelection() const noexcept {

  return selection_.Primary();
}

const std::vector<CameraTimelineKeyframeSelection> &
CameraTimelineKeyframeEditor::GetSelections() const noexcept {

  return selection_.Items();
}

bool CameraTimelineKeyframeEditor::IsInteractionActive() const noexcept {
  return dragSession_.IsActive() || marqueeActive_;
}

bool CameraTimelineKeyframeEditor::SelectAll(CinematicSequence &sequence) {

  std::vector<CameraTimelineKeyframeSelection> selections{};
  for (const SEQUENCER::CameraTransformChannel &channel :
       sequence.cameraTransformTrack.channels) {
    for (const SEQUENCER::CameraTransformKeyframe &keyframe :
         channel.keyframes) {
      selections.push_back({CameraTimelineKeyframeKind::Transform,
                            channel.cameraBindingId, keyframe.id});
    }
  }
  for (const SEQUENCER::CameraLensChannel &channel :
       sequence.cameraLensTrack.channels) {
    for (const SEQUENCER::CameraLensKeyframe &keyframe : channel.keyframes) {
      selections.push_back({CameraTimelineKeyframeKind::Lens,
                            channel.cameraBindingId, keyframe.id});
    }
  }
  return selection_.Apply(selections,
                          CameraTimelineSelectionOperation::Replace);
}

void CameraTimelineKeyframeEditor::SetSelections(
    const std::vector<CameraTimelineKeyframeSelection> &selections) {

  (void)selection_.Apply(selections, CameraTimelineSelectionOperation::Replace);
}

bool CameraTimelineKeyframeEditor::DeleteSelected(CinematicSequence &sequence) {

  if (!HasSelection()) {
    return false;
  }
  bool removed = false;
  for (SEQUENCER::CameraTransformChannel &channel :
       sequence.cameraTransformTrack.channels) {
    const size_t oldSize = channel.keyframes.size();
    channel.keyframes.erase(
        std::remove_if(channel.keyframes.begin(), channel.keyframes.end(),
                       [this, &channel](const auto &keyframe) {
                         return selection_.Contains(
                             {CameraTimelineKeyframeKind::Transform,
                              channel.cameraBindingId, keyframe.id});
                       }),
        channel.keyframes.end());
    removed |= channel.keyframes.size() != oldSize;
  }
  for (SEQUENCER::CameraLensChannel &channel :
       sequence.cameraLensTrack.channels) {
    const size_t oldSize = channel.keyframes.size();
    channel.keyframes.erase(
        std::remove_if(channel.keyframes.begin(), channel.keyframes.end(),
                       [this, &channel](const auto &keyframe) {
                         return selection_.Contains(
                             {CameraTimelineKeyframeKind::Lens,
                              channel.cameraBindingId, keyframe.id});
                       }),
        channel.keyframes.end());
    removed |= channel.keyframes.size() != oldSize;
  }
  if (removed) {
    SEQUENCER::NormalizeCameraTransformTrack(sequence.cameraTransformTrack);
    SEQUENCER::NormalizeCameraLensTrack(sequence.cameraLensTrack);
    ClearSelection();
  }
  return removed;
}

bool CameraTimelineKeyframeEditor::DrawSelectedKeyInspector(
    CinematicSequence &sequence, bool editingAllowed, bool snapEnabled,
    float snapFramesPerSecond) {

  return DrawCameraTimelineKeyframeInspector(sequence, GetSelections(),
                                             editingAllowed, snapEnabled,
                                             snapFramesPerSecond);
}

void CameraTimelineKeyframeEditor::SelectTransformKeyframe(
    SEQUENCER::SequenceBindingId bindingId, uint64_t keyframeId) noexcept {

  (void)selection_.Apply(
      {keyframeId != 0 ? CameraTimelineKeyframeKind::Transform
                       : CameraTimelineKeyframeKind::None,
       keyframeId != 0 ? bindingId : SEQUENCER::SequenceBindingId{},
       keyframeId},
      CameraTimelineSelectionOperation::Replace);
}

void CameraTimelineKeyframeEditor::SelectLensKeyframe(
    SEQUENCER::SequenceBindingId bindingId, uint64_t keyframeId) noexcept {

  (void)selection_.Apply(
      {keyframeId != 0 ? CameraTimelineKeyframeKind::Lens
                       : CameraTimelineKeyframeKind::None,
       keyframeId != 0 ? bindingId : SEQUENCER::SequenceBindingId{},
       keyframeId},
      CameraTimelineSelectionOperation::Replace);
}

} // namespace HIKARI::EDITOR
