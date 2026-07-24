#include "Scene/Sequencer/Tracks/HIKARI_CameraTransformTrack.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Scene/Sequencer/Tracks/Internal/HIKARI_CameraTrackNormalization.h"

namespace HIKARI::SEQUENCER {

    namespace {
        constexpr float kKeyframeTimeEpsilon = 0.0005f;

        MATH::Quat Slerp(
            MATH::Quat from,
            MATH::Quat to,
            float amount) noexcept {

            from = MATH::NormalizeQ(from);
            to = MATH::NormalizeQ(to);
            float dot = from.x * to.x + from.y * to.y +
                from.z * to.z + from.w * to.w;
            if (dot < 0.0f) {
                dot = -dot;
                to = { -to.x, -to.y, -to.z, -to.w };
            }
            if (dot > 0.9995f) {
                return MATH::NormalizeQ({
                    from.x + (to.x - from.x) * amount,
                    from.y + (to.y - from.y) * amount,
                    from.z + (to.z - from.z) * amount,
                    from.w + (to.w - from.w) * amount
                });
            }
            const float angle = std::acos(std::clamp(dot, -1.0f, 1.0f));
            const float sinAngle = std::sin(angle);
            if (std::abs(sinAngle) <= 0.00001f) {
                return from;
            }
            const float fromWeight = std::sin((1.0f - amount) * angle) /
                sinAngle;
            const float toWeight = std::sin(amount * angle) / sinAngle;
            return MATH::NormalizeQ({
                from.x * fromWeight + to.x * toWeight,
                from.y * fromWeight + to.y * toWeight,
                from.z * fromWeight + to.z * toWeight,
                from.w * fromWeight + to.w * toWeight
            });
        }

        MATH::Quat RotationFromDegrees(const MATH::Vec3& degrees) noexcept {
            constexpr float kDegreesToRadians =
                std::numbers::pi_v<float> / 180.0f;
            return MATH::Quat::FromEulerXYZ(
                degrees.x * kDegreesToRadians,
                degrees.y * kDegreesToRadians,
                degrees.z * kDegreesToRadians);
        }

    }

    void NormalizeCameraTransformTrack(CameraTransformTrack& track) {
        INTERNAL::NormalizeCameraTrack(
            track,
            2,
            [](const CameraTransformKeyframe& keyframe) {
                return std::isfinite(keyframe.timeSeconds) &&
                    MATH::IsFinite(keyframe.position) &&
                    MATH::IsFinite(keyframe.rotationEulerDeg);
            },
            [](CameraTransformKeyframe& keyframe) {
                keyframe.timeSeconds = std::clamp(
                    keyframe.timeSeconds,
                    0.0f,
                    kMaxSequenceDurationSeconds);
            });
    }

    uint64_t AllocateCameraTransformKeyframeId(
        const CameraTransformTrack& track) {

        uint64_t nextId = 1;
        for (const CameraTransformChannel& channel : track.channels) {
            for (const CameraTransformKeyframe& keyframe : channel.keyframes) {
                if (keyframe.id >= nextId &&
                    keyframe.id != (std::numeric_limits<uint64_t>::max)()) {
                    nextId = keyframe.id + 1;
                }
            }
        }
        return nextId == 0 ? 1 : nextId;
    }

    CameraTransformChannel* FindCameraTransformChannel(
        CameraTransformTrack& track,
        SequenceBindingId bindingId) noexcept {

        const auto found = std::find_if(
            track.channels.begin(),
            track.channels.end(),
            [bindingId](const CameraTransformChannel& channel) {
                return channel.cameraBindingId == bindingId;
            });
        return found != track.channels.end() ? &*found : nullptr;
    }

    const CameraTransformChannel* FindCameraTransformChannel(
        const CameraTransformTrack& track,
        SequenceBindingId bindingId) noexcept {

        const auto found = std::find_if(
            track.channels.begin(),
            track.channels.end(),
            [bindingId](const CameraTransformChannel& channel) {
                return channel.cameraBindingId == bindingId;
            });
        return found != track.channels.end() ? &*found : nullptr;
    }

    CameraTransformKeyframe* FindCameraTransformKeyframe(
        CameraTransformTrack& track,
        SequenceBindingId bindingId,
        uint64_t keyframeId) noexcept {

        CameraTransformChannel* channel =
            FindCameraTransformChannel(track, bindingId);
        if (channel == nullptr) {
            return nullptr;
        }
        const auto found = std::find_if(
            channel->keyframes.begin(),
            channel->keyframes.end(),
            [keyframeId](const CameraTransformKeyframe& keyframe) {
                return keyframe.id == keyframeId;
            });
        return found != channel->keyframes.end() ? &*found : nullptr;
    }

    bool MoveCameraTransformKeyframe(
        CameraTransformTrack& track,
        SequenceBindingId bindingId,
        uint64_t keyframeId,
        float timeSeconds) {

        if (!bindingId.IsValid() || keyframeId == 0 ||
            !std::isfinite(timeSeconds)) {
            return false;
        }
        CameraTransformChannel* channel =
            FindCameraTransformChannel(track, bindingId);
        if (channel == nullptr) {
            return false;
        }
        CameraTransformKeyframe* selected = FindCameraTransformKeyframe(
            track,
            bindingId,
            keyframeId);
        if (selected == nullptr) {
            return false;
        }
        const float safeTime = std::clamp(
            timeSeconds,
            0.0f,
            kMaxSequenceDurationSeconds);
        bool changed = selected->timeSeconds != safeTime;
        const size_t oldSize = channel->keyframes.size();
        channel->keyframes.erase(
            std::remove_if(
                channel->keyframes.begin(),
                channel->keyframes.end(),
                [keyframeId, safeTime](
                    const CameraTransformKeyframe& keyframe) {
                    return keyframe.id != keyframeId &&
                        std::abs(keyframe.timeSeconds - safeTime) <=
                            kKeyframeTimeEpsilon;
                }),
            channel->keyframes.end());
        changed |= channel->keyframes.size() != oldSize;
        selected = FindCameraTransformKeyframe(
            track,
            bindingId,
            keyframeId);
        if (selected == nullptr || !changed) {
            return changed;
        }
        selected->timeSeconds = safeTime;
        NormalizeCameraTransformTrack(track);
        return true;
    }

    uint64_t SetCameraTransformKeyframe(
        CameraTransformTrack& track,
        SequenceBindingId bindingId,
        float timeSeconds,
        const MATH::Vec3& position,
        const MATH::Vec3& rotationEulerDeg) {

        if (!bindingId.IsValid() || !std::isfinite(timeSeconds) ||
            !MATH::IsFinite(position) || !MATH::IsFinite(rotationEulerDeg)) {
            return 0;
        }
        CameraTransformChannel* channel =
            FindCameraTransformChannel(track, bindingId);
        if (channel == nullptr) {
            CameraTransformChannel newChannel{};
            newChannel.cameraBindingId = bindingId;
            track.channels.push_back(newChannel);
            channel = &track.channels.back();
        }
        const float safeTime = std::clamp(
            timeSeconds,
            0.0f,
            kMaxSequenceDurationSeconds);
        const auto existing = std::find_if(
            channel->keyframes.begin(),
            channel->keyframes.end(),
            [safeTime](const CameraTransformKeyframe& keyframe) {
                return std::abs(keyframe.timeSeconds - safeTime) <=
                    kKeyframeTimeEpsilon;
            });
        if (existing != channel->keyframes.end()) {
            existing->timeSeconds = safeTime;
            existing->position = position;
            existing->rotationEulerDeg = rotationEulerDeg;
            return existing->id;
        }

        CameraTransformKeyframe keyframe{};
        keyframe.id = AllocateCameraTransformKeyframeId(track);
        keyframe.timeSeconds = safeTime;
        keyframe.position = position;
        keyframe.rotationEulerDeg = rotationEulerDeg;
        channel->keyframes.push_back(keyframe);
        NormalizeCameraTransformTrack(track);
        return keyframe.id;
    }

    float GetCameraTransformTrackContentEnd(
        const CameraTransformTrack& track) noexcept {

        float contentEnd = 0.0f;
        for (const CameraTransformChannel& channel : track.channels) {
            for (const CameraTransformKeyframe& keyframe :
                    channel.keyframes) {
                if (std::isfinite(keyframe.timeSeconds)) {
                    contentEnd = (std::max)(
                        contentEnd,
                        keyframe.timeSeconds);
                }
            }
        }
        return contentEnd;
    }

    CameraTransformTrackEvaluation EvaluateCameraTransformTrack(
        const CameraTransformTrack& track,
        SequenceBindingId bindingId,
        float timeSeconds) noexcept {

        CameraTransformTrackEvaluation result{};
        if (!track.enabled || !bindingId.IsValid() ||
            !std::isfinite(timeSeconds)) {
            return result;
        }
        const CameraTransformChannel* channel =
            FindCameraTransformChannel(track, bindingId);
        if (channel == nullptr || channel->keyframes.empty()) {
            return result;
        }

        const auto upper = std::upper_bound(
            channel->keyframes.begin(),
            channel->keyframes.end(),
            timeSeconds,
            [](float time, const CameraTransformKeyframe& keyframe) {
                return time < keyframe.timeSeconds;
            });
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
            amount = duration > kKeyframeTimeEpsilon
                ? (timeSeconds - from->timeSeconds) / duration
                : 0.0f;
            amount = EvaluateSequenceInterpolation(
                from->interpolation,
                amount);
        }
        result.cameraBindingId = bindingId;
        result.position = from->position +
            (to->position - from->position) * amount;
        result.rotation = Slerp(
            RotationFromDegrees(from->rotationEulerDeg),
            RotationFromDegrees(to->rotationEulerDeg),
            amount);
        result.valid = true;
        return result;
    }

} // namespace HIKARI::SEQUENCER
