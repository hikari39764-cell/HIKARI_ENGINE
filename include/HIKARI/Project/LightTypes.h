#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

// 光源は局所光（点・スポット・管・長方形）だけ。強さは光束 [lm]、距離は逆二乗で range で滑らかに切る。
// 太陽と月は EnvironmentView が持つ。
// State 4 / Patch 3: 管と長方形の寸法、点・スポットの配光（IES）とスポットのクッキーを加えた。
inline constexpr uint32_t kGameProjectLightStateVersion = 4u;
inline constexpr uint32_t kGameProjectLightStateV4Size = 152u;
inline constexpr uint32_t kGameProjectLightPatchVersion = 3u;
inline constexpr uint32_t kGameProjectLightPatchV3Size = 144u;
// 資産 GUID（32 桁の 16 進と終端の NUL）。空文字列は「無し」。
inline constexpr uint32_t kGameProjectLightAssetGuidCapacity = 33u;

enum class GameProjectLightType : uint32_t {
    Point = 0u,
    Spot,
    // 管（線分、ローカル X）と長方形（ローカル X が幅、Y が高さ、+Z へ発光）の面光源。
    Tube,
    Rect,
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
    SourceRadius = 1u << 7u,
    CastShadows = 1u << 8u,
    // 管の長さ・長方形の幅（length）と長方形の高さ（height）。管と長方形だけ。
    AreaExtent = 1u << 9u,
    // 点・スポットの配光（LightProfile 資産）。
    IesProfile = 1u << 10u,
    // スポットのクッキー（Texture 資産）。
    Cookie = 1u << 11u,
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
    GameProjectLightPatchFields(GameProjectLightPatchField::SourceRadius) |
    GameProjectLightPatchFields(GameProjectLightPatchField::CastShadows) |
    GameProjectLightPatchFields(GameProjectLightPatchField::AreaExtent) |
    GameProjectLightPatchFields(GameProjectLightPatchField::IesProfile) |
    GameProjectLightPatchFields(GameProjectLightPatchField::Cookie);

// Runtime snapshot. The values belong to the World instance and are not
// persisted back to the Scene document. Initialize size/version before reading;
// the host rejects any other size/version.
struct GameProjectLightState {
    uint32_t structSize = kGameProjectLightStateV4Size;
    uint32_t version = kGameProjectLightStateVersion;
    GameProjectLightType type = GameProjectLightType::Point;
    uint8_t enabled = 1u;
    uint8_t reserved0[3] = {0u, 0u, 0u};
    GameProjectFloat3 color{1.0f, 1.0f, 1.0f};
    // Brightness in the light's own unit: luminous flux in lumens (a 60 W bulb is
    // about 800 lm), or for scene-relative lights a multiple of the environment's
    // illuminance at 1 m.
    float intensity = 800.0f;
    // Metres; inverse-square falloff fades smoothly to zero at this distance.
    float range = 10.0f;
    float innerConeAngleDegrees = 25.0f;
    float outerConeAngleDegrees = 35.0f;
    float angularFalloff = 1.0f;
    int32_t priority = 0;
    uint32_t reserved1 = 0u;
    uint64_t stateRevision = 0u;
    // Point/Spot physical emitter radius in world units; independent of Range.
    float sourceRadius = 0.0f;
    // Local-light shadow request; quality/resource budgets determine availability.
    uint32_t castsShadows = 0u;
    // Tube length or rect width (local X), and rect height (local Y), in metres.
    float length = 1.0f;
    float height = 1.0f;
    // Point/Spot photometric profile (LightProfile asset GUID); empty is isotropic.
    char iesProfileAssetGuid[kGameProjectLightAssetGuidCapacity]{};
    // Spot projected texture (Texture asset GUID); empty is none.
    char cookieAssetGuid[kGameProjectLightAssetGuidCapacity]{};
    uint8_t reserved2[6] = {0u, 0u, 0u, 0u, 0u, 0u};
};

// Atomic runtime-only update. Only fields selected by fieldMask are read.
struct GameProjectLightPatch {
    uint32_t structSize = kGameProjectLightPatchV3Size;
    uint32_t version = kGameProjectLightPatchVersion;
    uint32_t fieldMask = 0u;
    uint8_t enabled = 1u;
    uint8_t reserved0[3] = {0u, 0u, 0u};
    GameProjectFloat3 color{1.0f, 1.0f, 1.0f};
    // Same unit as GameProjectLightState::intensity.
    float intensity = 800.0f;
    float range = 10.0f;
    float innerConeAngleDegrees = 25.0f;
    float outerConeAngleDegrees = 35.0f;
    float angularFalloff = 1.0f;
    uint32_t reserved2 = 0u;
    // Read only for SourceRadius.
    float sourceRadius = 0.0f;
    // Read only for CastShadows. Must be 0 or 1.
    uint32_t castsShadows = 0u;
    uint32_t reserved1 = 0u;
    // Read only for AreaExtent (tube/rect lights).
    float length = 1.0f;
    float height = 1.0f;
    // Read only for IesProfile / Cookie. NUL-terminated asset GUID, or empty to clear.
    char iesProfileAssetGuid[kGameProjectLightAssetGuidCapacity]{};
    char cookieAssetGuid[kGameProjectLightAssetGuidCapacity]{};
    uint8_t reserved3[6] = {0u, 0u, 0u, 0u, 0u, 0u};
};

static_assert(std::is_standard_layout_v<GameProjectLightState>);
static_assert(std::is_trivially_copyable_v<GameProjectLightState>);
static_assert(sizeof(GameProjectLightState) == kGameProjectLightStateV4Size);
static_assert(offsetof(GameProjectLightState, color) == 16u);
static_assert(offsetof(GameProjectLightState, castsShadows) == 68u);
static_assert(offsetof(GameProjectLightState, stateRevision) == 56u);
static_assert(offsetof(GameProjectLightState, reserved1) == 52u);
static_assert(offsetof(GameProjectLightState, length) == 72u);
static_assert(offsetof(GameProjectLightState, iesProfileAssetGuid) == 80u);
static_assert(offsetof(GameProjectLightState, cookieAssetGuid) == 113u);
static_assert(std::is_standard_layout_v<GameProjectLightPatch>);
static_assert(std::is_trivially_copyable_v<GameProjectLightPatch>);
static_assert(sizeof(GameProjectLightPatch) == kGameProjectLightPatchV3Size);
static_assert(offsetof(GameProjectLightPatch, color) == 16u);
static_assert(offsetof(GameProjectLightPatch, reserved2) == 48u);
static_assert(offsetof(GameProjectLightPatch, sourceRadius) == 52u);
static_assert(offsetof(GameProjectLightPatch, castsShadows) == 56u);
static_assert(offsetof(GameProjectLightPatch, length) == 64u);
static_assert(offsetof(GameProjectLightPatch, iesProfileAssetGuid) == 72u);
static_assert(offsetof(GameProjectLightPatch, cookieAssetGuid) == 105u);

} // namespace HIKARI
