#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineShotEditor.h"

#include <algorithm>
#include <cmath>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

namespace {
constexpr float kShotEdgeGrabWidth = 7.0f;

SEQUENCER::CameraCutClip *FindShot(CinematicSequence &sequence,
                                   uint64_t shotId) noexcept {

  const auto found =
      std::find_if(sequence.cameraCutTrack.clips.begin(),
                   sequence.cameraCutTrack.clips.end(),
                   [shotId](const SEQUENCER::CameraCutClip &shot) {
                     return shot.id == shotId;
                   });
  return found != sequence.cameraCutTrack.clips.end() ? &*found : nullptr;
}

float SnapTime(float timeSeconds, bool enabled, int framesPerSecond) noexcept {

  if (!enabled || framesPerSecond <= 0) {
    return timeSeconds;
  }
  const float rate = static_cast<float>(framesPerSecond);
  return std::round(timeSeconds * rate) / rate;
}

const char *
CameraLabel(const SceneDocument &document, const CinematicSequence &sequence,
            const SEQUENCER::SequenceBindingContext *previewBindings,
            SEQUENCER::SequenceBindingId cameraBindingId) {

  SceneObjectId cameraObjectId{};
  const bool resolved =
      previewBindings != nullptr
          ? SEQUENCER::ResolveSceneObjectBinding(
                sequence.bindings, cameraBindingId, *previewBindings,
                cameraObjectId)
          : SEQUENCER::ResolveSceneObjectBinding(
                sequence.bindings, cameraBindingId, cameraObjectId);
  if (!resolved) {
    const SEQUENCER::SequenceBinding *binding =
        SEQUENCER::FindSequenceBinding(sequence.bindings, cameraBindingId);
    return binding != nullptr &&
                   binding->targetKind ==
                       SEQUENCER::SequenceBindingTargetKind::Slot &&
                   !binding->slotName.empty()
               ? binding->slotName.c_str()
               : "Unbound Camera";
  }
  const auto found =
      std::find_if(document.objects.begin(), document.objects.end(),
                   [cameraObjectId](const SceneObjectData &object) {
                     return object.id == cameraObjectId;
                   });
  return found != document.objects.end() && !found->name.empty()
             ? found->name.c_str()
             : "Missing Camera";
}

#if defined(HIKARI_WITH_EDITOR)
ImU32 ShotColor(SEQUENCER::SequenceBindingId cameraBindingId, bool selected) {

  const uint32_t hash = static_cast<uint32_t>(cameraBindingId.value ^
                                              (cameraBindingId.value >> 32u));
  const float hue = static_cast<float>(hash % 360u) / 360.0f;
  ImVec4 color{};
  ImGui::ColorConvertHSVtoRGB(hue, selected ? 0.55f : 0.48f,
                              selected ? 0.92f : 0.72f, color.x, color.y,
                              color.z);
  color.w = 1.0f;
  return ImGui::ColorConvertFloat4ToU32(color);
}
#endif
} // namespace

CameraTimelineShotEditorResult CameraTimelineShotEditor::Draw(
    const SceneDocument &document, CinematicSequence &sequence,
    const SEQUENCER::SequenceBindingContext *previewBindings,
    const CameraTimelineShotLayout &layout) {

  CameraTimelineShotEditorResult result{};
#if defined(HIKARI_WITH_EDITOR)
  SEQUENCER::CameraCutClip *hoveredShot = nullptr;
  DragMode hoveredDragMode = DragMode::None;
  const ImVec2 mousePosition = ImGui::GetIO().MousePos;
  ImDrawList &drawList = *ImGui::GetWindowDrawList();
  for (SEQUENCER::CameraCutClip &shot : sequence.cameraCutTrack.clips) {
    const float shotLeft = layout.timelineLeft +
                           (shot.startTimeSeconds - layout.scrollTimeSeconds) *
                               layout.pixelsPerSecond;
    const float shotRight =
        shotLeft + shot.durationSeconds * layout.pixelsPerSecond;
    const float visibleLeft = (std::max)(shotLeft, layout.timelineLeft);
    const float visibleRight = (std::min)(shotRight, layout.timelineRight);
    if (visibleRight <= visibleLeft) {
      continue;
    }

    const bool selected = shot.id == selectedShotId_;
    const ImVec2 shotMin{visibleLeft, layout.trackTop + 8.0f};
    const ImVec2 shotMax{visibleRight, layout.trackBottom - 8.0f};
    drawList.AddRectFilled(shotMin, shotMax,
                           ShotColor(shot.cameraBindingId, selected), 4.0f);
    if (shot.transition.mode == SEQUENCER::CameraCutTransitionMode::EaseInOut &&
        shot.transition.durationSeconds > 0.0f) {
      const float blendRight = (std::min)(
          shotLeft + shot.transition.durationSeconds * layout.pixelsPerSecond,
          shotMax.x);
      if (blendRight > shotMin.x) {
        drawList.AddRectFilled(shotMin, ImVec2(blendRight, shotMax.y),
                               IM_COL32(255, 255, 255, selected ? 58 : 42),
                               4.0f);
        drawList.AddLine(ImVec2(blendRight, shotMin.y),
                         ImVec2(shotMin.x, shotMax.y),
                         IM_COL32(235, 241, 255, 165), 1.0f);
      }
    }
    drawList.AddRect(shotMin, shotMax,
                     selected ? IM_COL32(255, 222, 126, 255)
                              : IM_COL32(180, 190, 205, 210),
                     4.0f, ImDrawFlags_None, selected ? 2.0f : 1.0f);
    drawList.PushClipRect(shotMin, shotMax, true);
    drawList.AddText(
        ImVec2(shotMin.x + 8.0f, shotMin.y + 9.0f),
        IM_COL32(244, 247, 251, 255),
        CameraLabel(document, sequence, previewBindings, shot.cameraBindingId));
    drawList.PopClipRect();

    if (mousePosition.x >= shotMin.x && mousePosition.x <= shotMax.x &&
        mousePosition.y >= shotMin.y && mousePosition.y <= shotMax.y) {
      hoveredShot = &shot;
      if (std::abs(mousePosition.x - shotLeft) <= kShotEdgeGrabWidth) {
        hoveredDragMode = DragMode::ResizeLeft;
      } else if (std::abs(mousePosition.x - shotRight) <= kShotEdgeGrabWidth) {
        hoveredDragMode = DragMode::ResizeRight;
      } else {
        hoveredDragMode = DragMode::Move;
      }
    }
  }

  if (layout.canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
      hoveredShot != nullptr) {
    result.selectionChanged = selectedShotId_ != hoveredShot->id;
    result.capturedLeftClick = true;
    selectedShotId_ = hoveredShot->id;
    if (layout.editingAllowed) {
      draggedShotId_ = hoveredShot->id;
      dragMode_ = hoveredDragMode;
      dragStartMouseX_ = mousePosition.x;
      dragStartShotTime_ = hoveredShot->startTimeSeconds;
      dragStartShotDuration_ = hoveredShot->durationSeconds;
    }
  }

  if (dragMode_ != DragMode::None) {
    if (layout.editingAllowed && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      if (SEQUENCER::CameraCutClip *shot = FindShot(sequence, draggedShotId_)) {
        const float deltaTimeSeconds =
            (mousePosition.x - dragStartMouseX_) / layout.pixelsPerSecond;
        const float originalEnd = dragStartShotTime_ + dragStartShotDuration_;
        switch (dragMode_) {
        case DragMode::Move:
          shot->startTimeSeconds = (std::max)(
              0.0f, SnapTime(dragStartShotTime_ + deltaTimeSeconds,
                             layout.snapEnabled, layout.snapFramesPerSecond));
          break;
        case DragMode::ResizeLeft:
          shot->startTimeSeconds = std::clamp(
              SnapTime(dragStartShotTime_ + deltaTimeSeconds,
                       layout.snapEnabled, layout.snapFramesPerSecond),
              0.0f, originalEnd - SEQUENCER::kMinCameraCutClipDurationSeconds);
          shot->durationSeconds = originalEnd - shot->startTimeSeconds;
          break;
        case DragMode::ResizeRight:
          shot->durationSeconds = (std::max)(
              SEQUENCER::kMinCameraCutClipDurationSeconds,
              SnapTime(originalEnd + deltaTimeSeconds, layout.snapEnabled,
                       layout.snapFramesPerSecond) -
                  shot->startTimeSeconds);
          break;
        default:
          break;
        }
        sequence.durationSeconds =
            (std::max)(sequence.durationSeconds,
                       shot->startTimeSeconds + shot->durationSeconds);
        result.sequenceChanged = true;
      }
    } else {
      CancelInteraction();
      NormalizeCinematicSequence(sequence);
    }
  }

  if (layout.canvasHovered && hoveredShot != nullptr) {
    ImGui::SetMouseCursor(hoveredDragMode == DragMode::Move
                              ? ImGuiMouseCursor_Hand
                              : ImGuiMouseCursor_ResizeEW);
    if (hoveredShot->transition.mode ==
        SEQUENCER::CameraCutTransitionMode::EaseInOut) {
      ImGui::SetTooltip("%s\nStart %.2f s  Duration %.2f s\nBlend %.2f s",
                        CameraLabel(document, sequence, previewBindings,
                                    hoveredShot->cameraBindingId),
                        hoveredShot->startTimeSeconds,
                        hoveredShot->durationSeconds,
                        hoveredShot->transition.durationSeconds);
    } else {
      ImGui::SetTooltip("%s\nStart %.2f s  Duration %.2f s\nCut",
                        CameraLabel(document, sequence, previewBindings,
                                    hoveredShot->cameraBindingId),
                        hoveredShot->startTimeSeconds,
                        hoveredShot->durationSeconds);
    }
  }
#else
  (void)document;
  (void)sequence;
  (void)previewBindings;
  (void)layout;
#endif
  return result;
}

void CameraTimelineShotEditor::Reset() {
  selectedShotId_ = 0;
  CancelInteraction();
}

void CameraTimelineShotEditor::CancelInteraction() {
  draggedShotId_ = 0;
  dragMode_ = DragMode::None;
}

uint64_t CameraTimelineShotEditor::GetSelectedShotId() const noexcept {
  return selectedShotId_;
}

void CameraTimelineShotEditor::SetSelectedShotId(uint64_t shotId) noexcept {

  selectedShotId_ = shotId;
}

void CameraTimelineShotEditor::ClearSelection() noexcept {
  selectedShotId_ = 0;
}

} // namespace HIKARI::EDITOR
