#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

    inline constexpr uint32_t kGameProjectLightStateVersion = 1u;
    inline constexpr uint32_t kGameProjectLightStateV1Size = 64u;
    inline constexpr uint32_t kGameProjectLightPatchVersion = 1u;
    inline constexpr uint32_t kGameProjectLightPatchV1Size = 64u;

    enum class GameProjectLightType : uint32_t {
        Directional = 0u,
        Point,
        Spot,
        Count,
    };

    enum class GameProjectLightPatchField : uint32_t {
        None = 0u,
        Enabled = 1u << 0u,
        Color = 1u << 1u,
        Intensity = 1u << 2u,
        Range = 1u << 3u,
        SpotCone = 1u << 4u,
        AngularFalloff = 1u << 5u,
    };

    constexpr GameProjectLightPatchField operator|(
        GameProjectLightPatchField lhs,
        GameProjectLightPatchField rhs) noexcept {
        return static_cast<GameProjectLightPatchField>(
            static_cast<uint32_t>(lhs) |
            static_cast<uint32_t>(rhs));
    }

    constexpr uint32_t GameProjectLightPatchFields(
        GameProjectLightPatchField fields) noexcept {
        return static_cast<uint32_t>(fields);
    }

    inline constexpr uint32_t kGameProjectLightPatchKnownFields =
        GameProjectLightPatchFields(GameProjectLightPatchField::Enabled) |
        GameProjectLightPatchFields(GameProjectLightPatchField::Color) |
        GameProjectLightPatchFields(GameProjectLightPatchField::Intensity) |
        GameProjectLightPatchFields(GameProjectLightPatchField::Range) |
        GameProjectLightPatchFields(GameProjectLightPatchField::SpotCone) |
        GameProjectLightPatchFields(
            GameProjectLightPatchField::AngularFalloff);

    // Runtime snapshot. The values belong to the World instance and are not
    // persisted back to the Scene document.
    struct GameProjectLightState {
        uint32_t structSize = kGameProjectLightStateV1Size;
        uint32_t version = kGameProjectLightStateVersion;
        GameProjectLightType type = GameProjectLightType::Point;
        uint8_t enabled = 1u;
        uint8_t reserved0[3] = { 0u, 0u, 0u };
        GameProjectFloat3 color{ 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
        float range = 5.0f;
        float innerConeAngleDegrees = 25.0f;
        float outerConeAngleDegrees = 35.0f;
        float angularFalloff = 1.0f;
        int32_t priority = 0;
        uint32_t reserved1 = 0u;
        uint64_t stateRevision = 0u;
    };

    // Atomic runtime-only update. Only fields selected by fieldMask are read.
    struct GameProjectLightPatch {
        uint32_t structSize = kGameProjectLightPatchV1Size;
        uint32_t version = kGameProjectLightPatchVersion;
        uint32_t fieldMask = 0u;
        uint8_t enabled = 1u;
        uint8_t reserved0[3] = { 0u, 0u, 0u };
        GameProjectFloat3 color{ 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
        float range = 5.0f;
        float innerConeAngleDegrees = 25.0f;
        float outerConeAngleDegrees = 35.0f;
        float angularFalloff = 1.0f;
        uint32_t reserved1[4] = { 0u, 0u, 0u, 0u };
    };

    static_assert(std::is_standard_layout_v<GameProjectLightState>);
    static_assert(std::is_trivially_copyable_v<GameProjectLightState>);
    static_assert(sizeof(GameProjectLightState) ==
        kGameProjectLightStateV1Size);
    static_assert(offsetof(GameProjectLightState, color) == 16u);
    static_assert(offsetof(GameProjectLightState, stateRevision) == 56u);
    static_assert(std::is_standard_layout_v<GameProjectLightPatch>);
    static_assert(std::is_trivially_copyable_v<GameProjectLightPatch>);
    static_assert(sizeof(GameProjectLightPatch) ==
        kGameProjectLightPatchV1Size);
    static_assert(offsetof(GameProjectLightPatch, color) == 16u);

} // namespace HIKARI
