#pragma once

#include <cstdint>

#include "Scene/HIKARI_CinematicSequence.h"
#include "Scene/Sequencer/HIKARI_SequencePlayback.h"

namespace HIKARI {

    using CinematicPlaybackState = SEQUENCER::SequencePlaybackState;
    using CinematicPlaybackOptions = SEQUENCER::SequencePlaybackOptions;

    class CinematicSequencePlayer {
    public:
        bool Bind(
            const SceneCinematicsSettings& settings,
            CinematicSequenceId sequenceId,
            float timeSeconds = 0.0f);
        bool Play(
            const SceneCinematicsSettings& settings,
            CinematicSequenceId sequenceId,
            float startTimeSeconds = 0.0f,
            const CinematicPlaybackOptions& options = {});
        bool Play();
        void Pause();
        bool Resume();
        void Stop();
        void Clear();
        bool Seek(
            const SceneCinematicsSettings& settings,
            float timeSeconds);

        SEQUENCER::SequenceEvaluationContext Tick(
            const SceneCinematicsSettings& settings,
            float deltaTime,
            SEQUENCER::SequenceEvaluationMode mode =
                SEQUENCER::SequenceEvaluationMode::Runtime);

        CinematicSequenceId GetSequenceId() const noexcept;
        CinematicPlaybackState GetState() const noexcept;
        float GetTimeSeconds() const noexcept;
        bool IsPlaying() const noexcept;
        bool IsPaused() const noexcept;
        bool CompletedThisTick() const noexcept;
        const SEQUENCER::SequenceEvaluationContext&
            GetLastEvaluationContext() const noexcept;

    private:
        CinematicSequenceId sequenceId_{};
        SEQUENCER::SequencePlaybackCursor playbackCursor_{};
    };

} // namespace HIKARI
