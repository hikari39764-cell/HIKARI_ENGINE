#pragma once

#include <cstdint>
#include <type_traits>

namespace HIKARI {

    enum class GameProjectAntiAliasing : uint8_t { Off, FXAA, TAA, DLAA, DLSS };
    enum class GameProjectDisplayResolution : uint8_t { Viewport, P720, P1080, P1440, P2160 };
    enum class GameProjectWindowMode : uint8_t { Windowed, Borderless, Fullscreen };
    enum class GameProjectDlssQuality : uint8_t { Quality, Balanced, Performance, UltraPerformance };

    struct GameProjectDisplaySettings {
        GameProjectAntiAliasing antiAliasing = GameProjectAntiAliasing::DLAA;
        GameProjectDisplayResolution renderResolution = GameProjectDisplayResolution::P1080;
        GameProjectDisplayResolution windowResolution = GameProjectDisplayResolution::P1080;
        GameProjectWindowMode windowMode = GameProjectWindowMode::Windowed;
        GameProjectDlssQuality dlssQuality = GameProjectDlssQuality::Quality;
        uint8_t frameGeneration = 0;
        uint8_t vSync = 0;
        uint8_t reserved = 0;
        bool operator==(const GameProjectDisplaySettings&) const noexcept = default;
    };

    struct GameProjectDisplayCapabilities {
        uint32_t antiAliasingMask = 0;
        uint32_t outputWidth = 0;
        uint32_t outputHeight = 0;
        uint8_t frameGenerationSupported = 0;
        uint8_t gameWindowActive = 0;
        uint8_t reserved[2]{};

        bool Supports(GameProjectAntiAliasing mode) const noexcept {
            const auto bit = static_cast<uint32_t>(mode);
            return bit <= static_cast<uint32_t>(GameProjectAntiAliasing::DLSS) &&
                (antiAliasingMask & (1u << bit)) != 0;
        }
    };

    enum class GameProjectDisplayApplyResult : uint8_t {
        Accepted, InvalidWorld, WrongThread, NotPlaying, InvalidValue, Unsupported
    };

    static_assert(std::is_trivially_copyable_v<GameProjectDisplaySettings>);
    static_assert(std::is_standard_layout_v<GameProjectDisplaySettings>);
    static_assert(sizeof(GameProjectDisplaySettings) == 8);
    static_assert(sizeof(GameProjectDisplayCapabilities) == 16);
}
