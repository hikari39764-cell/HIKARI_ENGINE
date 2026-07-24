#include "Scene/HIKARI_CinematicSequencePlayer.h"

namespace HIKARI {

    bool CinematicSequencePlayer::Bind(
        const SceneCinematicsSettings& settings,
        CinematicSequenceId sequenceId,
        float timeSeconds) {

        const CinematicSequence* sequence =
            FindCinematicSequence(settings, sequenceId);
        if (sequence == nullptr ||
            !playbackCursor_.Bind(
                sequence->durationSeconds,
                timeSeconds)) {
            Clear();
            return false;
        }
        sequenceId_ = sequenceId;
        return true;
    }

    bool CinematicSequencePlayer::Play(
        const SceneCinematicsSettings& settings,
        CinematicSequenceId sequenceId,
        float startTimeSeconds,
        const CinematicPlaybackOptions& options) {

        return Bind(settings, sequenceId, startTimeSeconds) &&
            playbackCursor_.Play(options);
    }

    bool CinematicSequencePlayer::Play() {
        return sequenceId_.IsValid() && playbackCursor_.Play();
    }

    void CinematicSequencePlayer::Pause() {
        playbackCursor_.Pause();
    }

    bool CinematicSequencePlayer::Resume() {
        return sequenceId_.IsValid() && playbackCursor_.Resume();
    }

    void CinematicSequencePlayer::Stop() {
        playbackCursor_.Stop();
    }

    void CinematicSequencePlayer::Clear() {
        sequenceId_ = {};
        playbackCursor_.Clear();
    }

    bool CinematicSequencePlayer::Seek(
        const SceneCinematicsSettings& settings,
        float timeSeconds) {

        const CinematicSequence* sequence =
            FindCinematicSequence(settings, sequenceId_);
        if (sequence == nullptr ||
            !playbackCursor_.SetDuration(sequence->durationSeconds)) {
            Clear();
            return false;
        }
        return playbackCursor_.Seek(timeSeconds);
    }

    SEQUENCER::SequenceEvaluationContext CinematicSequencePlayer::Tick(
        const SceneCinematicsSettings& settings,
        float deltaTime,
        SEQUENCER::SequenceEvaluationMode mode) {

        const CinematicSequence* sequence =
            FindCinematicSequence(settings, sequenceId_);
        if (sequence == nullptr ||
            !playbackCursor_.SetDuration(sequence->durationSeconds)) {
            Clear();
            return {};
        }
        return playbackCursor_.Tick(deltaTime, mode);
    }

    CinematicSequenceId CinematicSequencePlayer::GetSequenceId() const noexcept {
        return sequenceId_;
    }

    CinematicPlaybackState CinematicSequencePlayer::GetState() const noexcept {
        return playbackCursor_.GetState();
    }

    float CinematicSequencePlayer::GetTimeSeconds() const noexcept {
        return playbackCursor_.GetTimeSeconds();
    }

    bool CinematicSequencePlayer::IsPlaying() const noexcept {
        return playbackCursor_.IsPlaying();
    }

    bool CinematicSequencePlayer::IsPaused() const noexcept {
        return playbackCursor_.IsPaused();
    }

    bool CinematicSequencePlayer::CompletedThisTick() const noexcept {
        return playbackCursor_.CompletedThisTick();
    }

    const SEQUENCER::SequenceEvaluationContext&
        CinematicSequencePlayer::GetLastEvaluationContext() const noexcept {
        return playbackCursor_.GetLastEvaluationContext();
    }

} // namespace HIKARI
