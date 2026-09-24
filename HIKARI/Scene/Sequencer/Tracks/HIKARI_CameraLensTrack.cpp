#include "Scene/Sequencer/Tracks/HIKARI_CameraLensTrack.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Scene/Sequencer/Tracks/Internal/HIKARI_CameraTrackNormalization.h"

namespace HIKARI::SEQUENCER {

namespace {
constexpr float kKeyframeTimeEpsilon = 0.0005f;
constexpr float kMinVerticalFovDegrees = 1.0f;
constexpr float kMaxVerticalFovDegrees = 179.0f;
constexpr float kMinNearClip = 0.001f;
constexpr float kMinClipRange = 0.001f;

} // namespace

void NormalizeCameraLensTrack(CameraLensTrack& track) {
    INTERNAL::NormalizeCameraTrack(
        track, 3,
        [](const CameraLensKeyframe& keyframe) {
            return std::isfinite(keyframe.timeSeconds) && std::isfinite(keyframe.verticalFovDegrees) &&
                   std::isfinite(keyframe.nearClip) && std::isfinite(keyframe.farClip);
        },
        [](CameraLensKeyframe& keyframe) {
            keyframe.timeSeconds = std::clamp(keyframe.timeSeconds, 0.0f, kMaxSequenceDurationSeconds);
            keyframe.verticalFovDegrees =
                std::clamp(keyframe.verticalFovDegrees, kMinVerticalFovDegrees, kMaxVerticalFovDegrees);
            keyframe.nearClip = (std::max)(keyframe.nearClip, kMinNearClip);
            keyframe.farClip = (std::max)(keyframe.farClip, keyframe.nearClip + kMinClipRange);
        });
}

CameraLensChannel* FindCameraLensChannel(CameraLensTrack& track, SequenceBindingId bindingId) noexcept {
    const auto found =
        std::find_if(track.channels.begin(), track.channels.end(),
                     [bindingId](const CameraLensChannel& channel) { return channel.cameraBindingId == bindingId; });
    return found != track.channels.end() ? &*found : nullptr;
}

const CameraLensChannel* FindCameraLensChannel(const CameraLensTrack& track, SequenceBindingId bindingId) noexcept {
    const auto found =
        std::find_if(track.channels.begin(), track.channels.end(),
                     [bindingId](const CameraLensChannel& channel) { return channel.cameraBindingId == bindingId; });
    return found != track.channels.end() ? &*found : nullptr;
}

float GetCameraLensTrackContentEnd(const CameraLensTrack& track) noexcept {
    float contentEnd = 0.0f;
    for (const CameraLensChannel& channel : track.channels) {
        for (const CameraLensKeyframe& keyframe : channel.keyframes) {
            if (std::isfinite(keyframe.timeSeconds)) {
                contentEnd = (std::max)(contentEnd, keyframe.timeSeconds);
            }
        }
    }
    return contentEnd;
}

CameraLensTrackEvaluation EvaluateCameraLensTrack(const CameraLensTrack& track, SequenceBindingId bindingId,
                                                  float timeSeconds) noexcept {
    CameraLensTrackEvaluation result{};
    if (!track.enabled || !bindingId.IsValid() || !std::isfinite(timeSeconds)) {
        return result;
    }
    const CameraLensChannel* channel = FindCameraLensChannel(track, bindingId);
    if (channel == nullptr || channel->keyframes.empty()) {
        return result;
    }
    const auto upper =
        std::upper_bound(channel->keyframes.begin(), channel->keyframes.end(), timeSeconds,
                         [](float time, const CameraLensKeyframe& keyframe) { return time < keyframe.timeSeconds; });
    const CameraLensKeyframe* from = nullptr;
    const CameraLensKeyframe* to = nullptr;
    if (upper == channel->keyframes.begin()) {
        from = &channel->keyframes.front();
        to = from;
    } else if (upper == channel->keyframes.end()) {
        from = &channel->keyframes.back();
        to = from;
    } else {
        to = &*upper;
        from = &*(upper - 1);
    }

    float amount = 0.0f;
    if (from != to) {
        const float duration = to->timeSeconds - from->timeSeconds;
        amount = duration > kKeyframeTimeEpsilon ? (timeSeconds - from->timeSeconds) / duration : 0.0f;
        amount = EvaluateSequenceInterpolation(from->interpolation, amount);
    }
    result.cameraBindingId = bindingId;
    result.verticalFovDegrees = from->verticalFovDegrees + (to->verticalFovDegrees - from->verticalFovDegrees) * amount;
    result.nearClip = from->nearClip + (to->nearClip - from->nearClip) * amount;
    result.farClip = from->farClip + (to->farClip - from->farClip) * amount;
    result.nearClip = (std::max)(result.nearClip, kMinNearClip);
    result.farClip = (std::max)(result.farClip, result.nearClip + kMinClipRange);
    result.valid = true;
    return result;
}

} // namespace HIKARI::SEQUENCER
