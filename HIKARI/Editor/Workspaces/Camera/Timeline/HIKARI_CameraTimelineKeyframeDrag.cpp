#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineKeyframeDrag.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace HIKARI::EDITOR {

namespace {
constexpr float kKeyframeTimeEpsilon = 0.0005f;

bool FindSelectionTime(CinematicSequence &sequence,
                       const CameraTimelineKeyframeSelection &selection,
                       float &outTimeSeconds) {

  if (selection.kind == CameraTimelineKeyframeKind::Transform) {
    const auto *keyframe = SEQUENCER::FindCameraTransformKeyframe(
        sequence.cameraTransformTrack, selection.bindingId,
        selection.keyframeId);
    if (keyframe != nullptr) {
      outTimeSeconds = keyframe->timeSeconds;
      return true;
    }
  } else if (selection.kind == CameraTimelineKeyframeKind::Lens) {
    const auto *keyframe = SEQUENCER::FindCameraLensKeyframe(
        sequence.cameraLensTrack, selection.bindingId, selection.keyframeId);
    if (keyframe != nullptr) {
      outTimeSeconds = keyframe->timeSeconds;
      return true;
    }
  }
  return false;
}

template <typename Items>
void MoveTransformSelections(SEQUENCER::CameraTransformTrack &track,
                             const Items &items, float deltaSeconds) {

  for (SEQUENCER::CameraTransformChannel &channel : track.channels) {
    std::vector<float> targetTimes{};
    for (const auto &item : items) {
      if (item.selection.kind == CameraTimelineKeyframeKind::Transform &&
          item.selection.bindingId == channel.cameraBindingId) {
        targetTimes.push_back(item.startTimeSeconds + deltaSeconds);
      }
    }
    if (targetTimes.empty()) {
      continue;
    }
    channel.keyframes.erase(
        std::remove_if(
            channel.keyframes.begin(), channel.keyframes.end(),
            [&items, &channel, &targetTimes](const auto &keyframe) {
              const auto selected = std::find_if(
                  items.begin(), items.end(),
                  [&channel, &keyframe](const auto &item) {
                    return item.selection.kind ==
                               CameraTimelineKeyframeKind::Transform &&
                           item.selection.bindingId ==
                               channel.cameraBindingId &&
                           item.selection.keyframeId == keyframe.id;
                  });
              if (selected != items.end()) {
                return false;
              }
              return std::any_of(targetTimes.begin(), targetTimes.end(),
                                 [&keyframe](float timeSeconds) {
                                   return std::abs(keyframe.timeSeconds -
                                                   timeSeconds) <=
                                          kKeyframeTimeEpsilon;
                                 });
            }),
        channel.keyframes.end());
    for (SEQUENCER::CameraTransformKeyframe &keyframe : channel.keyframes) {
      const auto found = std::find_if(
          items.begin(), items.end(), [&channel, &keyframe](const auto &item) {
            return item.selection.kind ==
                       CameraTimelineKeyframeKind::Transform &&
                   item.selection.bindingId == channel.cameraBindingId &&
                   item.selection.keyframeId == keyframe.id;
          });
      if (found != items.end()) {
        keyframe.timeSeconds = found->startTimeSeconds + deltaSeconds;
      }
    }
  }
  SEQUENCER::NormalizeCameraTransformTrack(track);
}

template <typename Items>
void MoveLensSelections(SEQUENCER::CameraLensTrack &track, const Items &items,
                        float deltaSeconds) {

  for (SEQUENCER::CameraLensChannel &channel : track.channels) {
    std::vector<float> targetTimes{};
    for (const auto &item : items) {
      if (item.selection.kind == CameraTimelineKeyframeKind::Lens &&
          item.selection.bindingId == channel.cameraBindingId) {
        targetTimes.push_back(item.startTimeSeconds + deltaSeconds);
      }
    }
    if (targetTimes.empty()) {
      continue;
    }
    channel.keyframes.erase(
        std::remove_if(channel.keyframes.begin(), channel.keyframes.end(),
                       [&items, &channel, &targetTimes](const auto &keyframe) {
                         const auto selected = std::find_if(
                             items.begin(), items.end(),
                             [&channel, &keyframe](const auto &item) {
                               return item.selection.kind ==
                                          CameraTimelineKeyframeKind::Lens &&
                                      item.selection.bindingId ==
                                          channel.cameraBindingId &&
                                      item.selection.keyframeId == keyframe.id;
                             });
                         if (selected != items.end()) {
                           return false;
                         }
                         return std::any_of(
                             targetTimes.begin(), targetTimes.end(),
                             [&keyframe](float timeSeconds) {
                               return std::abs(keyframe.timeSeconds -
                                               timeSeconds) <=
                                      kKeyframeTimeEpsilon;
                             });
                       }),
        channel.keyframes.end());
    for (SEQUENCER::CameraLensKeyframe &keyframe : channel.keyframes) {
      const auto found = std::find_if(
          items.begin(), items.end(), [&channel, &keyframe](const auto &item) {
            return item.selection.kind == CameraTimelineKeyframeKind::Lens &&
                   item.selection.bindingId == channel.cameraBindingId &&
                   item.selection.keyframeId == keyframe.id;
          });
      if (found != items.end()) {
        keyframe.timeSeconds = found->startTimeSeconds + deltaSeconds;
      }
    }
  }
  SEQUENCER::NormalizeCameraLensTrack(track);
}
} // namespace

bool CameraTimelineKeyframeDragSession::Begin(
    CinematicSequence &sequence,
    const std::vector<CameraTimelineKeyframeSelection> &selections,
    float mouseX) {

  End();
  items_.reserve(selections.size());
  for (const CameraTimelineKeyframeSelection &selection : selections) {
    float startTimeSeconds = 0.0f;
    if (FindSelectionTime(sequence, selection, startTimeSeconds)) {
      items_.push_back({selection, startTimeSeconds});
    }
  }
  if (items_.empty()) {
    return false;
  }
  transformBaseline_ = sequence.cameraTransformTrack;
  lensBaseline_ = sequence.cameraLensTrack;
  startMouseX_ = mouseX;
  lastDeltaSeconds_ = 0.0f;
  active_ = true;
  return true;
}

bool CameraTimelineKeyframeDragSession::Update(
    CinematicSequence &sequence,
    const CameraTimelineKeyframeSelection &primarySelection, float mouseX,
    float pixelsPerSecond, float sequenceDurationSeconds, bool snapEnabled,
    float snapFramesPerSecond) {

  if (!active_ || items_.empty() || pixelsPerSecond <= 0.0f) {
    return false;
  }
  float minimumStart = (std::numeric_limits<float>::max)();
  float maximumStart = 0.0f;
  for (const Item &item : items_) {
    minimumStart = (std::min)(minimumStart, item.startTimeSeconds);
    maximumStart = (std::max)(maximumStart, item.startTimeSeconds);
  }
  float deltaSeconds = (mouseX - startMouseX_) / pixelsPerSecond;
  const auto primary = std::find_if(items_.begin(), items_.end(),
                                    [&primarySelection](const Item &item) {
                                      return item.selection == primarySelection;
                                    });
  if (snapEnabled && snapFramesPerSecond > 0.0f && primary != items_.end()) {
    const float primaryTime = primary->startTimeSeconds + deltaSeconds;
    deltaSeconds =
        std::round(primaryTime * snapFramesPerSecond) / snapFramesPerSecond -
        primary->startTimeSeconds;
  }
  deltaSeconds = std::clamp(deltaSeconds, -minimumStart,
                            sequenceDurationSeconds - maximumStart);
  if (std::abs(deltaSeconds - lastDeltaSeconds_) <= kKeyframeTimeEpsilon) {
    return false;
  }

  sequence.cameraTransformTrack = transformBaseline_;
  sequence.cameraLensTrack = lensBaseline_;
  MoveTransformSelections(sequence.cameraTransformTrack, items_, deltaSeconds);
  MoveLensSelections(sequence.cameraLensTrack, items_, deltaSeconds);
  lastDeltaSeconds_ = deltaSeconds;
  return true;
}

void CameraTimelineKeyframeDragSession::End() {
  items_.clear();
  transformBaseline_ = {};
  lensBaseline_ = {};
  startMouseX_ = 0.0f;
  lastDeltaSeconds_ = 0.0f;
  active_ = false;
}

bool CameraTimelineKeyframeDragSession::IsActive() const noexcept {
  return active_;
}

} // namespace HIKARI::EDITOR
