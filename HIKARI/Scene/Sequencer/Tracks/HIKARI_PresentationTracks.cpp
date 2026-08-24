#include "Scene/Sequencer/Tracks/HIKARI_PresentationTracks.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::SEQUENCER {

    namespace {
        float FiniteOr(float value, float fallback) noexcept {
            return std::isfinite(value) ? value : fallback;
        }

        template<class Item>
        uint64_t AllocateId(const std::vector<Item>& items) noexcept {
            uint64_t next = 1;
            for (const Item& item : items) {
                if (item.id != (std::numeric_limits<uint64_t>::max)()) {
                    next = (std::max)(next, item.id + 1);
                }
            }
            return next == 0 ? 1 : next;
        }

        template<class Item>
        void NormalizeIdsAndOrder(std::vector<Item>& items) {
            std::stable_sort(
                items.begin(),
                items.end(),
                [](const Item& lhs, const Item& rhs) {
                    if (lhs.startTimeSeconds != rhs.startTimeSeconds) {
                        return lhs.startTimeSeconds < rhs.startTimeSeconds;
                    }
                    return lhs.id < rhs.id;
                });
            std::unordered_set<uint64_t> used{};
            uint64_t next = AllocateId(items);
            for (Item& item : items) {
                if (item.id == 0 || !used.insert(item.id).second) {
                    while (next == 0 || used.contains(next)) {
                        ++next;
                    }
                    item.id = next++;
                    used.insert(item.id);
                }
            }
        }

        template<class Item>
        float ContentEnd(const std::vector<Item>& items) noexcept {
            float end = 0.0f;
            for (const Item& item : items) {
                end = (std::max)(
                    end,
                    item.startTimeSeconds + item.durationSeconds);
            }
            return end;
        }

        template<class Cue>
        void NormalizeCueIdsAndOrder(std::vector<Cue>& cues) {
            std::stable_sort(
                cues.begin(),
                cues.end(),
                [](const Cue& lhs, const Cue& rhs) {
                    if (lhs.timeSeconds != rhs.timeSeconds) {
                        return lhs.timeSeconds < rhs.timeSeconds;
                    }
                    return lhs.id < rhs.id;
                });
            std::unordered_set<uint64_t> used{};
            uint64_t next = AllocateId(cues);
            for (Cue& cue : cues) {
                if (cue.id == 0 || !used.insert(cue.id).second) {
                    while (next == 0 || used.contains(next)) {
                        ++next;
                    }
                    cue.id = next++;
                    used.insert(cue.id);
                }
            }
        }
    }

    void NormalizeActorAnimationTrack(ActorAnimationTrack& track) {
        for (ActorAnimationClip& clip : track.clips) {
            clip.startTimeSeconds = (std::max)(
                0.0f,
                FiniteOr(clip.startTimeSeconds, 0.0f));
            clip.durationSeconds = std::clamp(
                FiniteOr(clip.durationSeconds, 1.0f),
                kMinSequenceClipDurationSeconds,
                kMaxSequenceDurationSeconds);
            clip.sourceStartTimeSeconds = (std::max)(
                0.0f,
                FiniteOr(clip.sourceStartTimeSeconds, 0.0f));
            clip.playbackRate = std::clamp(
                FiniteOr(clip.playbackRate, 1.0f),
                -kMaxActorAnimationPlaybackRate,
                kMaxActorAnimationPlaybackRate);
            if (std::abs(clip.playbackRate) < 0.001f) {
                clip.playbackRate = 1.0f;
            }
            clip.blendInSeconds = std::clamp(
                FiniteOr(clip.blendInSeconds, 0.12f),
                0.0f,
                clip.durationSeconds);
        }
        NormalizeIdsAndOrder(track.clips);
    }

    void NormalizeSequenceVfxTrack(SequenceVfxTrack& track) {
        for (SequenceVfxCue& cue : track.cues) {
            cue.timeSeconds = (std::max)(
                0.0f,
                FiniteOr(cue.timeSeconds, 0.0f));
            cue.scale = std::clamp(
                FiniteOr(cue.scale, 1.0f),
                0.001f,
                1000.0f);
            if (!MATH::IsFinite(cue.positionOffset)) {
                cue.positionOffset = {};
            }
            if (!MATH::IsFinite(cue.rotationEulerDeg)) {
                cue.rotationEulerDeg = {};
            }
        }
        NormalizeCueIdsAndOrder(track.cues);
    }

    void NormalizeSequenceTimeDilationTrack(
        SequenceTimeDilationTrack& track) {

        for (SequenceTimeDilationClip& clip : track.clips) {
            clip.startTimeSeconds = (std::max)(
                0.0f,
                FiniteOr(clip.startTimeSeconds, 0.0f));
            clip.durationSeconds = std::clamp(
                FiniteOr(clip.durationSeconds, 0.25f),
                kMinSequenceClipDurationSeconds,
                kMaxSequenceDurationSeconds);
            clip.scale = std::clamp(
                FiniteOr(clip.scale, 0.25f),
                kMinSequenceTimeScale,
                1.0f);
        }
        NormalizeIdsAndOrder(track.clips);
    }

    void NormalizeSequenceSignalTrack(SequenceSignalTrack& track) {
        for (SequenceSignalCue& cue : track.cues) {
            cue.timeSeconds = (std::max)(
                0.0f,
                FiniteOr(cue.timeSeconds, 0.0f));
        }
        NormalizeCueIdsAndOrder(track.cues);
    }

    uint64_t AllocateActorAnimationClipId(
        const ActorAnimationTrack& track) noexcept {
        return AllocateId(track.clips);
    }

    uint64_t AllocateSequenceVfxCueId(
        const SequenceVfxTrack& track) noexcept {
        return AllocateId(track.cues);
    }

    uint64_t AllocateSequenceTimeDilationClipId(
        const SequenceTimeDilationTrack& track) noexcept {
        return AllocateId(track.clips);
    }

    uint64_t AllocateSequenceSignalCueId(
        const SequenceSignalTrack& track) noexcept {
        return AllocateId(track.cues);
    }

    float GetActorAnimationTrackContentEnd(
        const ActorAnimationTrack& track) noexcept {
        return ContentEnd(track.clips);
    }

    float GetSequenceVfxTrackContentEnd(
        const SequenceVfxTrack& track) noexcept {
        float end = 0.0f;
        for (const SequenceVfxCue& cue : track.cues) {
            end = (std::max)(end, cue.timeSeconds);
        }
        return end;
    }

    float GetSequenceTimeDilationTrackContentEnd(
        const SequenceTimeDilationTrack& track) noexcept {
        return ContentEnd(track.clips);
    }

    float GetSequenceSignalTrackContentEnd(
        const SequenceSignalTrack& track) noexcept {
        float end = 0.0f;
        for (const SequenceSignalCue& cue : track.cues) {
            end = (std::max)(end, cue.timeSeconds);
        }
        return end;
    }

    ActorAnimationTrackEvaluation EvaluateActorAnimationTrack(
        const ActorAnimationTrack& track,
        float timeSeconds) noexcept {

        ActorAnimationTrackEvaluation result{};
        if (!track.enabled || !std::isfinite(timeSeconds)) {
            return result;
        }
        for (const ActorAnimationClip& clip : track.clips) {
            const float end = clip.startTimeSeconds + clip.durationSeconds;
            if (timeSeconds < clip.startTimeSeconds || timeSeconds >= end) {
                continue;
            }
            result.clipId = clip.id;
            result.actorBindingId = clip.actorBindingId;
            result.clipName = clip.clipName;
            result.localTimeSeconds = timeSeconds - clip.startTimeSeconds;
            result.sourceTimeSeconds = clip.sourceStartTimeSeconds +
                result.localTimeSeconds * clip.playbackRate;
            result.playbackRate = clip.playbackRate;
            result.blendInSeconds = clip.blendInSeconds;
            result.loop = clip.loop;
        }
        return result;
    }

    const SequenceTimeDilationClip* EvaluateSequenceTimeDilationTrack(
        const SequenceTimeDilationTrack& track,
        float timeSeconds) noexcept {

        if (!track.enabled || !std::isfinite(timeSeconds)) {
            return nullptr;
        }
        const SequenceTimeDilationClip* winner = nullptr;
        for (const SequenceTimeDilationClip& clip : track.clips) {
            if (timeSeconds < clip.startTimeSeconds ||
                timeSeconds >= clip.startTimeSeconds + clip.durationSeconds) {
                continue;
            }
            if (winner == nullptr || clip.priority > winner->priority ||
                (clip.priority == winner->priority && clip.id < winner->id)) {
                winner = &clip;
            }
        }
        return winner;
    }

    bool DidSequenceTimeCrossCue(
        const SequenceEvaluationContext& context,
        float cueTimeSeconds,
        bool fireOnSeek) noexcept {

        if (!std::isfinite(cueTimeSeconds)) {
            return false;
        }
        constexpr float epsilon = 1.0f / 1000.0f;
        if (context.reason == SequenceEvaluationReason::Seek) {
            return fireOnSeek && std::abs(
                context.currentTimeSeconds - cueTimeSeconds) <= epsilon;
        }
        if (context.reason == SequenceEvaluationReason::Start) {
            return cueTimeSeconds >= context.previousTimeSeconds - epsilon &&
                cueTimeSeconds <= context.currentTimeSeconds + epsilon;
        }
        if (context.wrapped || context.reason ==
                SequenceEvaluationReason::Loop) {
            return cueTimeSeconds > context.previousTimeSeconds + epsilon ||
                cueTimeSeconds <= context.currentTimeSeconds + epsilon;
        }
        if (context.currentTimeSeconds >= context.previousTimeSeconds) {
            return cueTimeSeconds > context.previousTimeSeconds + epsilon &&
                cueTimeSeconds <= context.currentTimeSeconds + epsilon;
        }
        return cueTimeSeconds < context.previousTimeSeconds - epsilon &&
            cueTimeSeconds >= context.currentTimeSeconds - epsilon;
    }

} // namespace HIKARI::SEQUENCER
