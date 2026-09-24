#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

inline constexpr uint32_t kGameProjectLightStateVersion = 2u;
inline constexpr uint32_t kGameProjectLightStateV1Size = 64u;
inline constexpr uint32_t kGameProjectLightStateV2Size = 72u;
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
    DistanceFalloff = 1u << 6u,
    SourceRadius = 1u << 7u,
    CastShadows = 1u << 8u,
};

enum class GameProjectLightFalloff : uint32_t {
    LegacyRange = 0u,
    InverseSquare = 1u,
};

constexpr GameProjectLightPatchField operator|(GameProjectLightPatchField lhs,
                                               GameProjectLightPatchField rhs) noexcept {
    return static_cast<GameProjectLightPatchField>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr uint32_t GameProjectLightPatchFields(GameProjectLightPatchField fields) noexcept {
    return static_cast<uint32_t>(fields);
}

inline constexpr uint32_t kGameProjectLightPatchKnownFields =
    GameProjectLightPatchFields(GameProjectLightPatchField::Enabled) |
    GameProjectLightPatchFields(GameProjectLightPatchField::Color) |
    GameProjectLightPatchFields(GameProjectLightPatchField::Intensity) |
    GameProjectLightPatchFields(GameProjectLightPatchField::Range) |
    GameProjectLightPatchFields(GameProjectLightPatchField::SpotCone) |
    GameProjectLightPatchFields(GameProjectLightPatchField::AngularFalloff) |
    GameProjectLightPatchFields(GameProjectLightPatchField::DistanceFalloff) |
    GameProjectLightPatchFields(GameProjectLightPatchField::SourceRadius) |
    GameProjectLightPatchFields(GameProjectLightPatchField::CastShadows);

// Runtime snapshot. The values belong to the World instance and are not
// persisted back to the Scene document. Initialize size/version before reading.
// Hosts accept v1/64-byte and v2/72-byte callers and only write that prefix.
// A pre-2.27 host returns v1; sourceRadius/castsShadows are only valid in a v2 result.
struct GameProjectLightState {
    uint32_t structSize = kGameProjectLightStateV2Size;
    uint32_t version = kGameProjectLightStateVersion;
    GameProjectLightType type = GameProjectLightType::Point;
    uint8_t enabled = 1u;
    uint8_t reserved0[3] = {0u, 0u, 0u};
    GameProjectFloat3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 5.0f;
    float innerConeAngleDegrees = 25.0f;
    float outerConeAngleDegrees = 35.0f;
    float angularFalloff = 1.0f;
    int32_t priority = 0;
    // SDK 2.26 uses the previously reserved word; all v1 offsets are stable.
    GameProjectLightFalloff distanceFalloff = GameProjectLightFalloff::LegacyRange;
    uint64_t stateRevision = 0u;
    // Point/Spot physical emitter radius in world units; independent of Range.
    float sourceRadius = 0.0f;
    // Local-light shadow request; quality/resource budgets determine availability.
    uint32_t castsShadows = 0u;
};

// Atomic runtime-only update. Only fields selected by fieldMask are read.
struct GameProjectLightPatch {
    uint32_t structSize = kGameProjectLightPatchV1Size;
    uint32_t version = kGameProjectLightPatchVersion;
    uint32_t fieldMask = 0u;
    uint8_t enabled = 1u;
    uint8_t reserved0[3] = {0u, 0u, 0u};
    GameProjectFloat3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float range = 5.0f;
    float innerConeAngleDegrees = 25.0f;
    float outerConeAngleDegrees = 35.0f;
    float angularFalloff = 1.0f;
    // Read only when DistanceFalloff is selected; applies to Point/Spot.
    // Older hosts reject that field mask without applying any part of it.
    GameProjectLightFalloff distanceFalloff = GameProjectLightFalloff::LegacyRange;
    // SDK 2.27 consumes one reserved word without changing the v1 patch size.
    // Read only for SourceRadius; old hosts reject the unknown mask atomically.
    float sourceRadius = 0.0f;
    // Read only for CastShadows. Must be 0 or 1; applies to Point/Spot.
    uint32_t castsShadows = 0u;
    uint32_t reserved1 = 0u;
};

static_assert(std::is_standard_layout_v<GameProjectLightState>);
static_assert(std::is_trivially_copyable_v<GameProjectLightState>);
static_assert(sizeof(GameProjectLightState) == kGameProjectLightStateV2Size);
static_assert(offsetof(GameProjectLightState, color) == 16u);
static_assert(offsetof(GameProjectLightState, sourceRadius) == kGameProjectLightStateV1Size);
static_assert(offsetof(GameProjectLightState, castsShadows) == 68u);
static_assert(offsetof(GameProjectLightState, stateRevision) == 56u);
static_assert(offsetof(GameProjectLightState, distanceFalloff) == 52u);
static_assert(std::is_standard_layout_v<GameProjectLightPatch>);
static_assert(std::is_trivially_copyable_v<GameProjectLightPatch>);
static_assert(sizeof(GameProjectLightPatch) == kGameProjectLightPatchV1Size);
static_assert(offsetof(GameProjectLightPatch, color) == 16u);
static_assert(offsetof(GameProjectLightPatch, distanceFalloff) == 48u);
static_assert(offsetof(GameProjectLightPatch, sourceRadius) == 52u);
static_assert(offsetof(GameProjectLightPatch, castsShadows) == 56u);

} // namespace HIKARI
