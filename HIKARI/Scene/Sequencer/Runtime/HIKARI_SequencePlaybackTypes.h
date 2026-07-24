#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/Sequencer/HIKARI_SequenceBinding.h"
#include "Scene/Sequencer/HIKARI_SequencePlayback.h"

namespace HIKARI::SEQUENCER {

    struct SequencePlaybackHandle {
        uint64_t value = 0;
        uint64_t epoch = 0;

        bool IsValid() const noexcept {
            return value != 0 && epoch != 0;
        }

        bool operator==(const SequencePlaybackHandle& rhs) const noexcept =
            default;
    };

    enum class SequenceStopReason : uint8_t {
        Stopped,
        Replaced,
        Completed,
        Disabled,
        OwnerDestroyed,
        Reset,
    };

    enum class SequenceChannelPolicy : uint8_t {
        Parallel,
        Replace,
        ReplaceIfHigherOrEqual,
        RejectIfOccupied,
    };

    struct SequencePlayRequest {
        AssetGuid assetGuid{};
        SequenceBindingContext bindings{};
        SequencePlaybackOptions options{};
        std::string channel{};
        float startTimeSeconds = 0.0f;
        int priority = 0;
        SequenceChannelPolicy channelPolicy =
            SequenceChannelPolicy::ReplaceIfHigherOrEqual;
    };

    enum class SequenceDiagnosticSeverity : uint8_t {
        Info,
        Warning,
        Error,
    };

    struct SequencePlaybackDiagnostic {
        SequenceDiagnosticSeverity severity =
            SequenceDiagnosticSeverity::Info;
        std::string code{};
        std::string driverId{};
        std::string message{};
        SequenceBindingId bindingId{};
        std::string slotName{};
    };

    struct SequencePlayResult {
        SequencePlaybackHandle handle{};
        std::vector<SequencePlaybackDiagnostic> diagnostics{};

        bool IsAccepted() const noexcept {
            return handle.IsValid();
        }
    };

    struct SequencePlaybackSnapshot {
        SequencePlaybackHandle handle{};
        AssetGuid assetGuid{};
        SequencePlaybackState state = SequencePlaybackState::Stopped;
        float timeSeconds = 0.0f;
        float durationSeconds = 0.0f;
        std::string channel{};
        int priority = 0;
    };

    enum class SequencePlaybackEventKind : uint8_t {
        Started,
        Paused,
        Resumed,
        Seeked,
        Completed,
        Stopped,
        Rejected,
    };

    struct SequencePlaybackEvent {
        SequencePlaybackEventKind kind =
            SequencePlaybackEventKind::Rejected;
        SequencePlaybackHandle handle{};
        AssetGuid assetGuid{};
        uint64_t requestId = 0;
        float timeSeconds = 0.0f;
        SequenceStopReason stopReason = SequenceStopReason::Stopped;
        std::string message{};
    };

    enum class SequencePlaybackCommandKind : uint8_t {
        Play,
        Stop,
        Pause,
        Resume,
        Seek,
    };

    struct SequencePlaybackCommand {
        SequencePlaybackCommandKind kind =
            SequencePlaybackCommandKind::Play;
        uint64_t requestId = 0;
        SequencePlayRequest play{};
        SequencePlaybackHandle handle{};
        float timeSeconds = 0.0f;
    };

} // namespace HIKARI::SEQUENCER
