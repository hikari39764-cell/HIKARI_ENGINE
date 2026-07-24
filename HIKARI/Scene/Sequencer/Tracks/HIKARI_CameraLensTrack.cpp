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

    }

    void NormalizeCameraLensTrack(CameraLensTrack& track) {
        INTERNAL::NormalizeCameraTrack(
            track,
            3,
            [](const CameraLensKeyframe& keyframe) {
                return std::isfinite(keyframe.timeSeconds) &&
                    std::isfinite(keyframe.verticalFovDegrees) &&
                    std::isfinite(keyframe.nearClip) &&
                    std::isfinite(keyframe.farClip);
            },
            [](CameraLensKeyframe& keyframe) {
                keyframe.timeSeconds = std::clamp(
                    keyframe.timeSeconds,
                    0.0f,
                    kMaxSequenceDurationSeconds);
                keyframe.verticalFovDegrees = std::clamp(
                    keyframe.verticalFovDegrees,
                    kMinVerticalFovDegrees,
                    kMaxVerticalFovDegrees);
                keyframe.nearClip = (std::max)(
                    keyframe.nearClip,
                    kMinNearClip);
                keyframe.farClip = (std::max)(
                    keyframe.farClip,
                    keyframe.nearClip + kMinClipRange);
            });
    }

    uint64_t AllocateCameraLensKeyframeId(const CameraLensTrack& track) {
        uint64_t nextId = 1;
        for (const CameraLensChannel& channel : track.channels) {
            for (const CameraLensKeyframe& keyframe : channel.keyframes) {
                if (keyframe.id >= nextId &&
                    keyframe.id != (std::numeric_limits<uint64_t>::max)()) {
                    nextId = keyframe.id + 1;
                }
            }
        }
        return nextId == 0 ? 1 : nextId;
    }

    CameraLensChannel* FindCameraLensChannel(
        CameraLensTrack& track,
        SequenceBindingId bindingId) noexcept {

        const auto found = std::find_if(
            track.channels.begin(),
            track.channels.end(),
            [bindingId](const CameraLensChannel& channel) {
                return channel.cameraBindingId == bindingId;
            });
        return found != track.channels.end() ? &*found : nullptr;
    }

    const CameraLensChannel* FindCameraLensChannel(
        const CameraLensTrack& track,
        SequenceBindingId bindingId) noexcept {

        const auto found = std::find_if(
            track.channels.begin(),
            track.channels.end(),
            [bindingId](const CameraLensChannel& channel) {
                return channel.cameraBindingId == bindingId;
            });
        return found != track.channels.end() ? &*found : nullptr;
    }

    CameraLensKeyframe* FindCameraLensKeyframe(
        CameraLensTrack& track,
        SequenceBindingId bindingId,
        uint64_t keyframeId) noexcept {

        CameraLensChannel* channel = FindCameraLensChannel(track, bindingId);
        if (channel == nullptr) {
            return nullptr;
        }
        const auto found = std::find_if(
            channel->keyframes.begin(),
            channel->keyframes.end(),
            [keyframeId](const CameraLensKeyframe& keyframe) {
                return keyframe.id == keyframeId;
            });
        return found != channel->keyframes.end() ? &*found : nullptr;
    }

    bool MoveCameraLensKeyframe(
        CameraLensTrack& track,
        SequenceBindingId bindingId,
        uint64_t keyframeId,
        float timeSeconds) {

        if (!bindingId.IsValid() || keyframeId == 0 ||
            !std::isfinite(timeSeconds)) {
            return false;
        }
        CameraLensChannel* channel =
            FindCameraLensChannel(track, bindingId);
        if (channel == nullptr) {
            return false;
        }
        CameraLensKeyframe* selected = FindCameraLensKeyframe(
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
                    const CameraLensKeyframe& keyframe) {
                    return keyframe.id != keyframeId &&
                        std::abs(keyframe.timeSeconds - safeTime) <=
                            kKeyframeTimeEpsilon;
                }),
            channel->keyframes.end());
        changed |= channel->keyframes.size() != oldSize;
        selected = FindCameraLensKeyframe(
            track,
            bindingId,
            keyframeId);
        if (selected == nullptr || !changed) {
            return changed;
        }
        selected->timeSeconds = safeTime;
        NormalizeCameraLensTrack(track);
        return true;
    }

    uint64_t SetCameraLensKeyframe(
        CameraLensTrack& track,
        SequenceBindingId bindingId,
        float timeSeconds,
        float verticalFovDegrees,
        float nearClip,
        float farClip) {

        if (!bindingId.IsValid() || !std::isfinite(timeSeconds) ||
            !std::isfinite(verticalFovDegrees) ||
            !std::isfinite(nearClip) || !std::isfinite(farClip)) {
            return 0;
        }
        CameraLensChannel* channel = FindCameraLensChannel(track, bindingId);
        if (channel == nullptr) {
            CameraLensChannel newChannel{};
            newChannel.cameraBindingId = bindingId;
            track.channels.push_back(newChannel);
            channel = &track.channels.back();
        }
        const float safeTime = std::clamp(
            timeSeconds,
            0.0f,
            kMaxSequenceDurationSeconds);
        const float safeFov = std::clamp(
            verticalFovDegrees,
            kMinVerticalFovDegrees,
            kMaxVerticalFovDegrees);
        const float safeNearClip = (std::max)(nearClip, kMinNearClip);
        const float safeFarClip = (std::max)(
            farClip,
            safeNearClip + kMinClipRange);
        const auto existing = std::find_if(
            channel->keyframes.begin(),
            channel->keyframes.end(),
            [safeTime](const CameraLensKeyframe& keyframe) {
                return std::abs(keyframe.timeSeconds - safeTime) <=
                    kKeyframeTimeEpsilon;
            });
        if (existing != channel->keyframes.end()) {
            existing->timeSeconds = safeTime;
            existing->verticalFovDegrees = safeFov;
            existing->nearClip = safeNearClip;
            existing->farClip = safeFarClip;
            return existing->id;
        }

        CameraLensKeyframe keyframe{};
        keyframe.id = AllocateCameraLensKeyframeId(track);
        keyframe.timeSeconds = safeTime;
        keyframe.verticalFovDegrees = safeFov;
        keyframe.nearClip = safeNearClip;
        keyframe.farClip = safeFarClip;
        channel->keyframes.push_back(keyframe);
        NormalizeCameraLensTrack(track);
        return keyframe.id;
    }

    float GetCameraLensTrackContentEnd(
        const CameraLensTrack& track) noexcept {

        float contentEnd = 0.0f;
        for (const CameraLensChannel& channel : track.channels) {
            for (const CameraLensKeyframe& keyframe : channel.keyframes) {
                if (std::isfinite(keyframe.timeSeconds)) {
                    contentEnd = (std::max)(
                        contentEnd,
                        keyframe.timeSeconds);
                }
            }
        }
        return contentEnd;
    }

    CameraLensTrackEvaluation EvaluateCameraLensTrack(
        const CameraLensTrack& track,
        SequenceBindingId bindingId,
        float timeSeconds) noexcept {

        CameraLensTrackEvaluation result{};
        if (!track.enabled || !bindingId.IsValid() ||
            !std::isfinite(timeSeconds)) {
            return result;
        }
        const CameraLensChannel* channel =
            FindCameraLensChannel(track, bindingId);
        if (channel == nullptr || channel->keyframes.empty()) {
            return result;
        }
        const auto upper = std::upper_bound(
            channel->keyframes.begin(),
            channel->keyframes.end(),
            timeSeconds,
            [](float time, const CameraLensKeyframe& keyframe) {
                return time < keyframe.timeSeconds;
            });
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
            amount = duration > kKeyframeTimeEpsilon
                ? (timeSeconds - from->timeSeconds) / duration
                : 0.0f;
            amount = EvaluateSequenceInterpolation(
                from->interpolation,
                amount);
        }
        result.cameraBindingId = bindingId;
        result.verticalFovDegrees = from->verticalFovDegrees +
            (to->verticalFovDegrees - from->verticalFovDegrees) * amount;
        result.nearClip = from->nearClip +
            (to->nearClip - from->nearClip) * amount;
        result.farClip = from->farClip +
            (to->farClip - from->farClip) * amount;
        result.nearClip = (std::max)(result.nearClip, kMinNearClip);
        result.farClip = (std::max)(
            result.farClip,
            result.nearClip + kMinClipRange);
        result.valid = true;
        return result;
    }

} // namespace HIKARI::SEQUENCER
