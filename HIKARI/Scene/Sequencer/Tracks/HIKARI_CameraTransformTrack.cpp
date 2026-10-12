#include "Scene/Sequencer/Tracks/HIKARI_CameraTransformTrack.h"

#include "Core/HIKARI_MathConfig.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/Sequencer/Tracks/Internal/HIKARI_CameraTrackNormalization.h"

namespace HIKARI::SEQUENCER {

namespace {
constexpr float kKeyframeTimeEpsilon = 0.0005f;

MATH::Quat RotationFromDegrees(const MATH::Vec3& degrees) noexcept {
    constexpr float kDegreesToRadians = MATH::kDegreesToRadians;
    return MATH::Quat::FromEulerXYZ(degrees.x * kDegreesToRadians, degrees.y * kDegreesToRadians,
                                    degrees.z * kDegreesToRadians);
}

} // namespace

void NormalizeCameraTransformTrack(CameraTransformTrack& track) {
    INTERNAL::NormalizeCameraTrack(
        track, 2,
        [](const CameraTransformKeyframe& keyframe) {
            return std::isfinite(keyframe.timeSeconds) && MATH::IsFinite(keyframe.position) &&
                   MATH::IsFinite(keyframe.rotationEulerDeg);
        },
        [](CameraTransformKeyframe& keyframe) {
            keyframe.timeSeconds = std::clamp(keyframe.timeSeconds, 0.0f, kMaxSequenceDurationSeconds);
        });
}

CameraTransformChannel* FindCameraTransformChannel(CameraTransformTrack& track, SequenceBindingId bindingId) noexcept {
    const auto found =
        std::find_if(track.channels.begin(), track.channels.end(), [bindingId](const CameraTransformChannel& channel) {
            return channel.cameraBindingId == bindingId;
        });
    return found != track.channels.end() ? &*found : nullptr;
}

const CameraTransformChannel* FindCameraTransformChannel(const CameraTransformTrack& track,
                                                         SequenceBindingId bindingId) noexcept {
    const auto found =
        std::find_if(track.channels.begin(), track.channels.end(), [bindingId](const CameraTransformChannel& channel) {
            return channel.cameraBindingId == bindingId;
        });
    return found != track.channels.end() ? &*found : nullptr;
}

float GetCameraTransformTrackContentEnd(const CameraTransformTrack& track) noexcept {
    float contentEnd = 0.0f;
    for (const CameraTransformChannel& channel : track.channels) {
        for (const CameraTransformKeyframe& keyframe : channel.keyframes) {
            if (std::isfinite(keyframe.timeSeconds)) {
                contentEnd = (std::max)(contentEnd, keyframe.timeSeconds);
            }
        }
    }
    return contentEnd;
}

CameraTransformTrackEvaluation EvaluateCameraTransformTrack(const CameraTransformTrack& track,
                                                            SequenceBindingId bindingId, float timeSeconds) noexcept {
    CameraTransformTrackEvaluation result{};
    if (!track.enabled || !bindingId.IsValid() || !std::isfinite(timeSeconds)) {
        return result;
    }
    const CameraTransformChannel* channel = FindCameraTransformChannel(track, bindingId);
    if (channel == nullptr || channel->keyframes.empty()) {
        return result;
    }

    const auto upper = std::upper_bound(
        channel->keyframes.begin(), channel->keyframes.end(), timeSeconds,
        [](float time, const CameraTransformKeyframe& keyframe) { return time < keyframe.timeSeconds; });
    const CameraTransformKeyframe* from = nullptr;
    const CameraTransformKeyframe* to = nullptr;
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
    result.position = from->position + (to->position - from->position) * amount;
    result.rotation =
        MATH::SlerpQ(RotationFromDegrees(from->rotationEulerDeg), RotationFromDegrees(to->rotationEulerDeg), amount);
    result.valid = true;
    return result;
}

} // namespace HIKARI::SEQUENCER
