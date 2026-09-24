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

MATH::Quat Slerp(MATH::Quat from, MATH::Quat to, float amount) noexcept {
    from = MATH::NormalizeQ(from);
    to = MATH::NormalizeQ(to);
    float dot = from.x * to.x + from.y * to.y + from.z * to.z + from.w * to.w;
    if (dot < 0.0f) {
        dot = -dot;
        to = {-to.x, -to.y, -to.z, -to.w};
    }
    if (dot > 0.9995f) {
        return MATH::NormalizeQ({from.x + (to.x - from.x) * amount, from.y + (to.y - from.y) * amount,
                                 from.z + (to.z - from.z) * amount, from.w + (to.w - from.w) * amount});
    }
    const float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));
    const float sinAngle = std::sin(angle);
    if (std::abs(sinAngle) <= 0.00001f) {
        return from;
    }
    const float fromWeight = std::sin((1.0f - amount) * angle) / sinAngle;
    const float toWeight = std::sin(amount * angle) / sinAngle;
    return MATH::NormalizeQ({from.x * fromWeight + to.x * toWeight, from.y * fromWeight + to.y * toWeight,
                             from.z * fromWeight + to.z * toWeight, from.w * fromWeight + to.w * toWeight});
}

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
        Slerp(RotationFromDegrees(from->rotationEulerDeg), RotationFromDegrees(to->rotationEulerDeg), amount);
    result.valid = true;
    return result;
}

} // namespace HIKARI::SEQUENCER
