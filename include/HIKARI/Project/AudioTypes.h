#pragma once

#include <cstdint>

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

} // namespace HIKARI
