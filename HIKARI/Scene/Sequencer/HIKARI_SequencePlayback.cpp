#include "Scene/Sequencer/HIKARI_SequencePlayback.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::SEQUENCER {

    namespace {
        float ClampPlaybackRate(float playbackRate) noexcept {
            if (!std::isfinite(playbackRate)) {
                return 1.0f;
            }
            return std::clamp(playbackRate, 0.01f, 8.0f);
        }

        bool IsValidDuration(float durationSeconds) noexcept {
            return std::isfinite(durationSeconds) && durationSeconds > 0.0f;
        }
    }

    bool SequencePlaybackCursor::Bind(
        float durationSeconds,
        float timeSeconds) {

        if (!IsValidDuration(durationSeconds)) {
            Clear();
            return false;
        }

        durationSeconds_ = durationSeconds;
        timeSeconds_ = std::clamp(
            std::isfinite(timeSeconds) ? timeSeconds : 0.0f,
            0.0f,
            durationSeconds_);
        state_ = SequencePlaybackState::Stopped;
        options_ = {};
        lastContext_ = {};
        lastContext_.currentTimeSeconds = timeSeconds_;
        lastContext_.previousTimeSeconds = timeSeconds_;
        lastContext_.durationSeconds = durationSeconds_;
        pendingReason_ = SequenceEvaluationReason::None;
        pendingPreviousTimeSeconds_ = timeSeconds_;
        bound_ = true;
        return true;
    }

    bool SequencePlaybackCursor::SetDuration(float durationSeconds) {
        if (!bound_ || !IsValidDuration(durationSeconds)) {
            return false;
        }
        durationSeconds_ = durationSeconds;
        timeSeconds_ = std::clamp(timeSeconds_, 0.0f, durationSeconds_);
        return true;
    }

    bool SequencePlaybackCursor::Play() {
        if (!bound_) {
            return false;
        }
        const SequenceEvaluationReason reason =
            state_ == SequencePlaybackState::Paused
                ? SequenceEvaluationReason::Resume
                : SequenceEvaluationReason::Start;
        state_ = SequencePlaybackState::Playing;
        SetPendingReason(reason, timeSeconds_);
        return true;
    }

    bool SequencePlaybackCursor::Play(
        const SequencePlaybackOptions& options) {

        if (!bound_) {
            return false;
        }
        options_ = options;
        options_.playbackRate = ClampPlaybackRate(options.playbackRate);
        state_ = SequencePlaybackState::Playing;
        SetPendingReason(SequenceEvaluationReason::Start, timeSeconds_);
        return true;
    }

    void SequencePlaybackCursor::Pause() {
        if (state_ == SequencePlaybackState::Playing) {
            state_ = SequencePlaybackState::Paused;
        }
    }

    bool SequencePlaybackCursor::Resume() {
        if (!bound_ || state_ != SequencePlaybackState::Paused) {
            return false;
        }
        state_ = SequencePlaybackState::Playing;
        SetPendingReason(SequenceEvaluationReason::Resume, timeSeconds_);
        return true;
    }

    void SequencePlaybackCursor::Stop() {
        if (!bound_) {
            return;
        }
        const float previousTimeSeconds = timeSeconds_;
        state_ = SequencePlaybackState::Stopped;
        timeSeconds_ = 0.0f;
        SetPendingReason(
            SequenceEvaluationReason::Stop,
            previousTimeSeconds);
    }

    void SequencePlaybackCursor::Clear() {
        state_ = SequencePlaybackState::Stopped;
        options_ = {};
        lastContext_ = {};
        pendingReason_ = SequenceEvaluationReason::None;
        pendingPreviousTimeSeconds_ = 0.0f;
        timeSeconds_ = 0.0f;
        durationSeconds_ = 0.0f;
        bound_ = false;
    }

    bool SequencePlaybackCursor::Seek(float timeSeconds) {
        if (!bound_) {
            return false;
        }
        const float previousTimeSeconds = timeSeconds_;
        timeSeconds_ = std::clamp(
            std::isfinite(timeSeconds) ? timeSeconds : 0.0f,
            0.0f,
            durationSeconds_);
        SetPendingReason(
            SequenceEvaluationReason::Seek,
            previousTimeSeconds);
        return true;
    }

    SequenceEvaluationContext SequencePlaybackCursor::Tick(
        float deltaTime,
        SequenceEvaluationMode mode) {

        SequenceEvaluationContext context{};
        context.currentTimeSeconds = timeSeconds_;
        context.previousTimeSeconds = timeSeconds_;
        context.durationSeconds = durationSeconds_;
        context.mode = mode;
        context.playing = state_ == SequencePlaybackState::Playing;

        if (!bound_) {
            lastContext_ = context;
            return lastContext_;
        }

        const SequenceEvaluationReason pendingReason = pendingReason_;
        if (pendingReason != SequenceEvaluationReason::None) {
            context.reason = pendingReason;
            context.previousTimeSeconds = pendingPreviousTimeSeconds_;
            pendingReason_ = SequenceEvaluationReason::None;
            if (pendingReason == SequenceEvaluationReason::Seek ||
                pendingReason == SequenceEvaluationReason::Stop) {
                context.currentTimeSeconds = timeSeconds_;
                lastContext_ = context;
                return lastContext_;
            }
        } else {
            context.reason = SequenceEvaluationReason::Tick;
        }

        if (state_ == SequencePlaybackState::Playing) {
            const float safeDeltaTime = std::isfinite(deltaTime)
                ? std::clamp(deltaTime, 0.0f, 0.25f)
                : 0.0f;
            context.previousTimeSeconds = timeSeconds_;
            timeSeconds_ += safeDeltaTime * options_.playbackRate;
            if (timeSeconds_ >= durationSeconds_) {
                if (options_.loop) {
                    timeSeconds_ = std::fmod(timeSeconds_, durationSeconds_);
                    context.reason = SequenceEvaluationReason::Loop;
                    context.wrapped = true;
                } else {
                    timeSeconds_ = durationSeconds_;
                    state_ = SequencePlaybackState::Stopped;
                    context.reason = SequenceEvaluationReason::Complete;
                    context.completed = true;
                }
            }
        }

        context.currentTimeSeconds = timeSeconds_;
        context.playing = state_ == SequencePlaybackState::Playing;
        lastContext_ = context;
        return lastContext_;
    }

    bool SequencePlaybackCursor::IsBound() const noexcept {
        return bound_;
    }

    SequencePlaybackState SequencePlaybackCursor::GetState() const noexcept {
        return state_;
    }

    float SequencePlaybackCursor::GetTimeSeconds() const noexcept {
        return timeSeconds_;
    }

    float SequencePlaybackCursor::GetDurationSeconds() const noexcept {
        return durationSeconds_;
    }

    bool SequencePlaybackCursor::IsPlaying() const noexcept {
        return state_ == SequencePlaybackState::Playing;
    }

    bool SequencePlaybackCursor::IsPaused() const noexcept {
        return state_ == SequencePlaybackState::Paused;
    }

    bool SequencePlaybackCursor::CompletedThisTick() const noexcept {
        return lastContext_.completed;
    }

    const SequenceEvaluationContext&
        SequencePlaybackCursor::GetLastEvaluationContext() const noexcept {
        return lastContext_;
    }

    void SequencePlaybackCursor::SetPendingReason(
        SequenceEvaluationReason reason,
        float previousTimeSeconds) noexcept {

        pendingReason_ = reason;
        pendingPreviousTimeSeconds_ = previousTimeSeconds;
        lastContext_.completed = false;
    }

} // namespace HIKARI::SEQUENCER
