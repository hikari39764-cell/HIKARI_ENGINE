#pragma once

#include <cstdint>
#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

    enum class GameProjectAudioBus : uint8_t {
        Bgm = 0u,
        Se = 1u,
        Voice = 2u,
    };

    struct GameProjectAudioSoundHandle {
        uint64_t value = 0u;
        constexpr bool IsValid() const noexcept { return value != 0u; }
    };

    struct GameProjectAudioPlaybackHandle {
        uint64_t value = 0u;
        constexpr bool IsValid() const noexcept { return value != 0u; }
    };

    // World units are metres. Point sources use mono audio; stereo music stays 2D.
    struct GameProjectAudioListener {
        GameProjectFloat3 position{};
        GameProjectFloat3 forward{0.0f, 0.0f, 1.0f};
        GameProjectFloat3 up{0.0f, 1.0f, 0.0f};
    };

    struct GameProjectAudioSource {
        GameProjectFloat3 position{};
        float minimumDistance = 1.0f;
        float maximumDistance = 30.0f;
    };

    enum class GameProjectAudioConcurrency : uint8_t {
        RejectNew,
        ReplaceOldest,
    };

    // One cue is a sound plus these editable playback defaults. No graph or
    // engine-specific object is needed. Group zero means this sound's group.
    struct GameProjectAudioPlayOptions {
        uint32_t structSize = sizeof(GameProjectAudioPlayOptions);
        uint32_t version = 1u;
        GameProjectAudioBus bus = GameProjectAudioBus::Se;
        uint8_t loop = 0u;
        uint8_t spatial = 0u;
        GameProjectAudioConcurrency concurrency = GameProjectAudioConcurrency::RejectNew;
        float volume = 1.0f;
        uint32_t group = 0u;
        uint32_t maximumInstances = 8u;
        int32_t priority = 0;
        GameProjectAudioSource source{};
        float startSeconds = 0.0f;
        float playbackRate = 1.0f;
        uint32_t paused = 0u;
    };

    struct GameProjectAudioStats {
        uint32_t activeVoices = 0u;
        uint32_t spatialVoices = 0u;
        uint32_t pausedVoices = 0u;
        uint32_t streamingVoices = 0u;
        uint32_t rejectedPlays = 0u;
        uint32_t stolenVoices = 0u;
        uint64_t residentBytes = 0u;
        uint64_t streamBufferBytes = 0u;
    };

} // namespace HIKARI
