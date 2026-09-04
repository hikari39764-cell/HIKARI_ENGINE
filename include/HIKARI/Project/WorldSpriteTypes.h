#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

    inline constexpr uint32_t kGameProjectWorldSpriteStateVersion = 1u;
    inline constexpr uint32_t kGameProjectWorldSpriteStateV1Size = 72u;
    inline constexpr uint32_t kGameProjectWorldSpritePatchVersion = 1u;
    inline constexpr uint32_t kGameProjectWorldSpritePatchV1Size = 72u;

    enum class GameProjectWorldSpriteFacingMode : uint32_t {
        Fixed = 0u,
        CameraFacing,
        FixedYAxis,
        Count,
    };

    enum class GameProjectWorldSpriteAlphaMode : uint32_t {
        Auto = 0u,
        Opaque,
        Mask,
        Blend,
        Count,
    };

    enum class GameProjectWorldSpritePatchField : uint32_t {
        None = 0u,
        Visible = 1u << 0u,
        Size = 1u << 1u,
        PreserveAspect = 1u << 2u,
        Facing = 1u << 3u,
        Tint = 1u << 4u,
        Opacity = 1u << 5u,
        Pivot = 1u << 6u,
        AlphaMode = 1u << 7u,
        AlphaCutoff = 1u << 8u,
        TransparentSortPriority = 1u << 9u,
    };

    constexpr GameProjectWorldSpritePatchField operator|(
        GameProjectWorldSpritePatchField lhs,
        GameProjectWorldSpritePatchField rhs) noexcept {
        return static_cast<GameProjectWorldSpritePatchField>(
            static_cast<uint32_t>(lhs) |
            static_cast<uint32_t>(rhs));
    }

    constexpr uint32_t GameProjectWorldSpritePatchFields(
        GameProjectWorldSpritePatchField fields) noexcept {
        return static_cast<uint32_t>(fields);
    }

    inline constexpr uint32_t kGameProjectWorldSpritePatchKnownFields =
        GameProjectWorldSpritePatchFields(
            GameProjectWorldSpritePatchField::Visible) |
        GameProjectWorldSpritePatchFields(
            GameProjectWorldSpritePatchField::Size) |
        GameProjectWorldSpritePatchFields(
            GameProjectWorldSpritePatchField::PreserveAspect) |
        GameProjectWorldSpritePatchFields(
            GameProjectWorldSpritePatchField::Facing) |
        GameProjectWorldSpritePatchFields(
            GameProjectWorldSpritePatchField::Tint) |
        GameProjectWorldSpritePatchFields(
            GameProjectWorldSpritePatchField::Opacity) |
        GameProjectWorldSpritePatchFields(
            GameProjectWorldSpritePatchField::Pivot) |
        GameProjectWorldSpritePatchFields(
            GameProjectWorldSpritePatchField::AlphaMode) |
        GameProjectWorldSpritePatchFields(
            GameProjectWorldSpritePatchField::AlphaCutoff) |
        GameProjectWorldSpritePatchFields(
            GameProjectWorldSpritePatchField::TransparentSortPriority);

    // Runtime snapshot of an authored WorldSpriteComponent. Runtime changes
    // belong to the World instance and never persist to the Scene document.
    struct GameProjectWorldSpriteState {
        uint32_t structSize = kGameProjectWorldSpriteStateV1Size;
        uint32_t version = kGameProjectWorldSpriteStateVersion;
        uint64_t stateRevision = 0u;
        GameProjectFloat2 size{ 1.0f, 1.0f };
        GameProjectFloat3 tint{ 1.0f, 1.0f, 1.0f };
        float opacity = 1.0f;
        GameProjectFloat2 pivot{ 0.5f, 0.5f };
        float alphaCutoff = 0.5f;
        int32_t transparentSortPriority = 0;
        GameProjectWorldSpriteFacingMode facing =
            GameProjectWorldSpriteFacingMode::CameraFacing;
        GameProjectWorldSpriteAlphaMode alphaMode =
            GameProjectWorldSpriteAlphaMode::Auto;
        uint8_t visible = 1u;
        uint8_t preserveAspect = 1u;
        uint8_t reserved[6] = { 0u, 0u, 0u, 0u, 0u, 0u };
    };

    // Atomic runtime-only update. Only fields selected by fieldMask are read.
    // Numeric values must be finite; the component applies its documented
    // clamps after the complete request has passed validation.
    struct GameProjectWorldSpritePatch {
        uint32_t structSize = kGameProjectWorldSpritePatchV1Size;
        uint32_t version = kGameProjectWorldSpritePatchVersion;
        uint32_t fieldMask = 0u;
        uint32_t reserved0 = 0u;
        GameProjectFloat2 size{ 1.0f, 1.0f };
        GameProjectFloat3 tint{ 1.0f, 1.0f, 1.0f };
        float opacity = 1.0f;
        GameProjectFloat2 pivot{ 0.5f, 0.5f };
        float alphaCutoff = 0.5f;
        int32_t transparentSortPriority = 0;
        GameProjectWorldSpriteFacingMode facing =
            GameProjectWorldSpriteFacingMode::CameraFacing;
        GameProjectWorldSpriteAlphaMode alphaMode =
            GameProjectWorldSpriteAlphaMode::Auto;
        uint8_t visible = 1u;
        uint8_t preserveAspect = 1u;
        uint8_t reserved1[6] = { 0u, 0u, 0u, 0u, 0u, 0u };
    };

    static_assert(std::is_standard_layout_v<GameProjectWorldSpriteState>);
    static_assert(std::is_trivially_copyable_v<GameProjectWorldSpriteState>);
    static_assert(sizeof(GameProjectWorldSpriteState) ==
        kGameProjectWorldSpriteStateV1Size);
    static_assert(offsetof(GameProjectWorldSpriteState, size) == 16u);
    static_assert(offsetof(GameProjectWorldSpriteState, visible) == 64u);
    static_assert(std::is_standard_layout_v<GameProjectWorldSpritePatch>);
    static_assert(std::is_trivially_copyable_v<GameProjectWorldSpritePatch>);
    static_assert(sizeof(GameProjectWorldSpritePatch) ==
        kGameProjectWorldSpritePatchV1Size);
    static_assert(offsetof(GameProjectWorldSpritePatch, size) == 16u);
    static_assert(offsetof(GameProjectWorldSpritePatch, visible) == 64u);

} // namespace HIKARI
