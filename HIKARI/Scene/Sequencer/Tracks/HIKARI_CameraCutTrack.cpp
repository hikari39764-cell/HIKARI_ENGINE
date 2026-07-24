#include "Scene/Sequencer/Tracks/HIKARI_CameraCutTrack.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace HIKARI::SEQUENCER {

    namespace {
        float FiniteOr(float value, float fallback) noexcept {
            return std::isfinite(value) ? value : fallback;
        }
    }

    void NormalizeCameraCutTrack(CameraCutTrack& track) {
        if (!track.id.IsValid()) {
            track.id = { 1 };
        }
        track.clips.erase(
            std::remove_if(
                track.clips.begin(),
                track.clips.end(),
                [](const CameraCutClip& clip) {
                    return clip.id == 0 ||
                        !clip.cameraBindingId.IsValid();
                }),
            track.clips.end());

        for (CameraCutClip& clip : track.clips) {
            clip.startTimeSeconds = std::clamp(
                FiniteOr(clip.startTimeSeconds, 0.0f),
                0.0f,
                kMaxSequenceDurationSeconds -
                    kMinCameraCutClipDurationSeconds);
            clip.durationSeconds = std::clamp(
                FiniteOr(
                    clip.durationSeconds,
                    kMinCameraCutClipDurationSeconds),
                kMinCameraCutClipDurationSeconds,
                kMaxSequenceDurationSeconds - clip.startTimeSeconds);
            if (clip.transition.mode == CameraCutTransitionMode::Cut) {
                clip.transition.durationSeconds = 0.0f;
            } else {
                clip.transition.durationSeconds = std::clamp(
                    FiniteOr(clip.transition.durationSeconds, 0.0f),
                    0.0f,
                    (std::min)(
                        clip.durationSeconds,
                        kMaxCameraBlendDurationSeconds));
            }
        }

        std::sort(
            track.clips.begin(),
            track.clips.end(),
            [](const CameraCutClip& lhs, const CameraCutClip& rhs) {
                if (lhs.startTimeSeconds != rhs.startTimeSeconds) {
                    return lhs.startTimeSeconds < rhs.startTimeSeconds;
                }
                return lhs.id < rhs.id;
            });
    }

    uint64_t AllocateCameraCutClipId(const CameraCutTrack& track) {
        uint64_t nextId = 1;
        for (const CameraCutClip& clip : track.clips) {
            if (clip.id < nextId) {
                continue;
            }
            if (clip.id == (std::numeric_limits<uint64_t>::max)()) {
                nextId = 1;
                break;
            }
            nextId = clip.id + 1;
        }
        while (std::any_of(
                track.clips.begin(),
                track.clips.end(),
                [nextId](const CameraCutClip& clip) {
                    return clip.id == nextId;
                })) {
            ++nextId;
            if (nextId == 0) {
                nextId = 1;
            }
        }
        return nextId;
    }

    float GetCameraCutTrackContentEnd(
        const CameraCutTrack& track) noexcept {

        float contentEnd = 0.0f;
        for (const CameraCutClip& clip : track.clips) {
            const float start = FiniteOr(clip.startTimeSeconds, 0.0f);
            const float duration = FiniteOr(clip.durationSeconds, 0.0f);
            contentEnd = (std::max)(contentEnd, start + duration);
        }
        return contentEnd;
    }

    CameraCutTrackEvaluation EvaluateCameraCutTrack(
        const CameraCutTrack& track,
        float timeSeconds) noexcept {

        CameraCutTrackEvaluation result{};
        if (!track.enabled || !std::isfinite(timeSeconds) ||
            timeSeconds < 0.0f) {
            return result;
        }

        const CameraCutClip* activeClip = nullptr;
        for (const CameraCutClip& clip : track.clips) {
            if (clip.id == 0 || !clip.cameraBindingId.IsValid() ||
                !std::isfinite(clip.startTimeSeconds) ||
                !std::isfinite(clip.durationSeconds) ||
                clip.durationSeconds < kMinCameraCutClipDurationSeconds) {
                continue;
            }
            const float clipEnd =
                clip.startTimeSeconds + clip.durationSeconds;
            if (timeSeconds < clip.startTimeSeconds ||
                timeSeconds >= clipEnd) {
                continue;
            }
            if (activeClip == nullptr ||
                clip.startTimeSeconds > activeClip->startTimeSeconds ||
                (clip.startTimeSeconds == activeClip->startTimeSeconds &&
                    clip.id > activeClip->id)) {
                activeClip = &clip;
            }
        }

        if (activeClip != nullptr) {
            result.clipId = activeClip->id;
            result.cameraBindingId = activeClip->cameraBindingId;
            result.transition = activeClip->transition;
            result.sequenceTimeSeconds = timeSeconds;
            result.localTimeSeconds =
                timeSeconds - activeClip->startTimeSeconds;
        }
        return result;
    }

} // namespace HIKARI::SEQUENCER
