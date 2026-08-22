#pragma once

#include <cstdint>
#include <type_traits>

namespace HIKARI {

    enum class GameProjectUiEffect : uint8_t {
        None = 0u,
        HorizontalVitalFill = 1u,
        StaminaFill = 2u,
        SkillCooldownEnergy = 3u,
        SkillReadyPulse = 4u,
        WeaponSwitchIn = 5u,
        WeaponSwitchOut = 6u,
        CounterAvailable = 7u,
        CounterPressed = 8u,
        RejectedPulse = 9u,
    };

    struct GameProjectUiColor {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    // All positions and extents are expressed in the caller's reference
    // canvas. The engine performs one aspect-preserving fit so authored HUD
    // layouts remain stable at non-16:9 output sizes.
    struct GameProjectUiSpriteRequest {
        uint64_t sourceId = 0u;
        int32_t order = 0;
        uint32_t flags = 0u;

        float referenceWidth = 1920.0f;
        float referenceHeight = 1080.0f;
        float x = 0.0f;
        float y = 0.0f;
        float width = 1.0f;
        float height = 1.0f;
        float pivotX = 0.0f;
        float pivotY = 0.0f;
        float rotationRadians = 0.0f;

        // fillAmount is the visible vital fraction for fill effects and the
        // remaining cooldown fraction for SkillCooldownEnergy.
        float fillAmount = 1.0f;
        float effectProgress = 1.0f;
        float effectIntensity = 1.0f;
        float timeSeconds = 0.0f;
        float directionX = 1.0f;
        float directionY = 0.0f;

        GameProjectUiColor tint{};
        GameProjectUiColor effectColor{
            0.0156863f,
            0.1686275f,
            0.5058824f,
            1.0f };
        GameProjectUiColor highlightColor{};
        GameProjectUiEffect effect = GameProjectUiEffect::None;
        uint8_t reserved[7]{};
    };

    static_assert(std::is_standard_layout_v<GameProjectUiColor>);
    static_assert(std::is_trivially_copyable_v<GameProjectUiColor>);
    static_assert(std::is_standard_layout_v<GameProjectUiSpriteRequest>);
    static_assert(std::is_trivially_copyable_v<GameProjectUiSpriteRequest>);

} // namespace HIKARI
