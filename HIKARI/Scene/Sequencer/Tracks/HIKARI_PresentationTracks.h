#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_SceneObjectId.h"
#include "Scene/Sequencer/HIKARI_SequenceBinding.h"
#include "Scene/Sequencer/HIKARI_SequencePlayback.h"
#include "Scene/Sequencer/HIKARI_SequenceTrack.h"

namespace HIKARI::SEQUENCER {

    struct ActorAnimationClip {
        uint64_t id = 0;
        SequenceBindingId actorBindingId{};
        std::string clipName{};
        float startTimeSeconds = 0.0f;
        float durationSeconds = 1.0f;
        float sourceStartTimeSeconds = 0.0f;
        float playbackRate = 1.0f;
        float blendInSeconds = 0.12f;
        bool loop = false;
    };

    struct ActorAnimationTrack {
        SequenceTrackId id{ 4 };
        bool enabled = true;
        std::vector<ActorAnimationClip> clips{};
    };

    struct ActorAnimationTrackEvaluation {
        uint64_t clipId = 0;
        SequenceBindingId actorBindingId{};
        std::string_view clipName{};
        float localTimeSeconds = 0.0f;
        float sourceTimeSeconds = 0.0f;
        float playbackRate = 1.0f;
        float blendInSeconds = 0.0f;
        bool loop = false;

        bool IsValid() const noexcept {
            return clipId != 0 && actorBindingId.IsValid() &&
                !clipName.empty();
        }
    };

    struct SequenceVfxCue {
        uint64_t id = 0;
        SequenceBindingId ownerBindingId{};
        AssetGuid assetGuid{};
        float timeSeconds = 0.0f;
        MATH::Vec3 positionOffset{};
        MATH::Vec3 rotationEulerDeg{};
        float scale = 1.0f;
        uint32_t randomSeed = 0;
        bool fireOnSeek = false;
    };

    struct SequenceVfxTrack {
        SequenceTrackId id{ 5 };
        bool enabled = true;
        std::vector<SequenceVfxCue> cues{};
    };

    struct SequenceTimeDilationClip {
        uint64_t id = 0;
        float startTimeSeconds = 0.0f;
        float durationSeconds = 0.25f;
        float scale = 0.25f;
        int32_t priority = 250;
    };

    struct SequenceTimeDilationTrack {
        SequenceTrackId id{ 6 };
        bool enabled = true;
        std::vector<SequenceTimeDilationClip> clips{};
    };

    struct SequenceSignalCue {
        uint64_t id = 0;
        SequenceBindingId targetBindingId{};
        std::string name{};
        std::string payload{};
        float timeSeconds = 0.0f;
        bool fireOnSeek = false;
    };

    struct SequenceSignalTrack {
        SequenceTrackId id{ 7 };
        bool enabled = true;
        std::vector<SequenceSignalCue> cues{};
    };

    struct SequenceSignalEvent {
        uint64_t cueId = 0;
        AssetGuid sequenceAssetGuid{};
        SceneObjectId targetObjectId{};
        std::string name{};
        std::string payload{};
    };

    constexpr float kMinSequenceClipDurationSeconds = 1.0f / 240.0f;
    constexpr float kMaxActorAnimationPlaybackRate = 8.0f;
    constexpr float kMinSequenceTimeScale = 0.01f;

    void NormalizeActorAnimationTrack(ActorAnimationTrack& track);
    void NormalizeSequenceVfxTrack(SequenceVfxTrack& track);
    void NormalizeSequenceTimeDilationTrack(
        SequenceTimeDilationTrack& track);
    void NormalizeSequenceSignalTrack(SequenceSignalTrack& track);

    uint64_t AllocateActorAnimationClipId(
        const ActorAnimationTrack& track) noexcept;
    uint64_t AllocateSequenceVfxCueId(
        const SequenceVfxTrack& track) noexcept;
    uint64_t AllocateSequenceTimeDilationClipId(
        const SequenceTimeDilationTrack& track) noexcept;
    uint64_t AllocateSequenceSignalCueId(
        const SequenceSignalTrack& track) noexcept;

    float GetActorAnimationTrackContentEnd(
        const ActorAnimationTrack& track) noexcept;
    float GetSequenceVfxTrackContentEnd(
        const SequenceVfxTrack& track) noexcept;
    float GetSequenceTimeDilationTrackContentEnd(
        const SequenceTimeDilationTrack& track) noexcept;
    float GetSequenceSignalTrackContentEnd(
        const SequenceSignalTrack& track) noexcept;

    ActorAnimationTrackEvaluation EvaluateActorAnimationTrack(
        const ActorAnimationTrack& track,
        float timeSeconds) noexcept;
    const SequenceTimeDilationClip* EvaluateSequenceTimeDilationTrack(
        const SequenceTimeDilationTrack& track,
        float timeSeconds) noexcept;
    bool DidSequenceTimeCrossCue(
        const SequenceEvaluationContext& context,
        float cueTimeSeconds,
        bool fireOnSeek) noexcept;

} // namespace HIKARI::SEQUENCER
