#pragma once

#include <cstdint>

namespace HIKARI::SEQUENCER {

    enum class SequencePlaybackState : uint8_t {
        Stopped,
        Playing,
        Paused,
    };

    enum class SequenceEvaluationReason : uint8_t {
        None,
        Start,
        Resume,
        Tick,
        Seek,
        Loop,
        Stop,
        Complete,
    };

    enum class SequenceEvaluationMode : uint8_t {
        Runtime,
        Preview,
    };

    struct SequencePlaybackOptions {
        bool loop = false;
        float playbackRate = 1.0f;
    };

    struct SequenceEvaluationContext {
        float previousTimeSeconds = 0.0f;
        float currentTimeSeconds = 0.0f;
        float durationSeconds = 0.0f;
        SequenceEvaluationReason reason = SequenceEvaluationReason::None;
        SequenceEvaluationMode mode = SequenceEvaluationMode::Runtime;
        bool playing = false;
        bool wrapped = false;
        bool completed = false;

        bool IsPreview() const noexcept {
            return mode == SequenceEvaluationMode::Preview;
        }

        bool IsDiscontinuous() const noexcept {
            return reason == SequenceEvaluationReason::Seek ||
                reason == SequenceEvaluationReason::Loop ||
                reason == SequenceEvaluationReason::Stop;
        }
    };

    class SequencePlaybackCursor {
    public:
        bool Bind(float durationSeconds, float timeSeconds = 0.0f);
        bool SetDuration(float durationSeconds);
        bool Play();
        bool Play(const SequencePlaybackOptions& options);
        void Pause();
        bool Resume();
        void Stop();
        void Clear();
        bool Seek(float timeSeconds);

        SequenceEvaluationContext Tick(
            float deltaTime,
            SequenceEvaluationMode mode);

        bool IsBound() const noexcept;
        SequencePlaybackState GetState() const noexcept;
        float GetTimeSeconds() const noexcept;
        float GetDurationSeconds() const noexcept;
        bool IsPlaying() const noexcept;
        bool IsPaused() const noexcept;
        bool CompletedThisTick() const noexcept;
        const SequenceEvaluationContext& GetLastEvaluationContext() const noexcept;

    private:
        void SetPendingReason(
            SequenceEvaluationReason reason,
            float previousTimeSeconds) noexcept;

        SequencePlaybackState state_ = SequencePlaybackState::Stopped;
        SequencePlaybackOptions options_{};
        SequenceEvaluationContext lastContext_{};
        SequenceEvaluationReason pendingReason_ =
            SequenceEvaluationReason::None;
        float pendingPreviousTimeSeconds_ = 0.0f;
        float timeSeconds_ = 0.0f;
        float durationSeconds_ = 0.0f;
        bool bound_ = false;
    };

} // namespace HIKARI::SEQUENCER
