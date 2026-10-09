#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

// 面へ投影するデカール（DecalComponent）。物体の原点を中心にした size の箱へ、ローカル +Z の向きに投影する
// （投影の向きに見てテクスチャの上がローカル +Y、鏡像にならない）。不透明のメッシュと地形の面だけが受ける。
inline constexpr uint32_t kGameProjectDecalStateVersion = 1u;
inline constexpr uint32_t kGameProjectDecalStateV1Size = 136u;
inline constexpr uint32_t kGameProjectDecalPatchVersion = 1u;
inline constexpr uint32_t kGameProjectDecalPatchV1Size = 140u;
// 資産 GUID（32 桁の 16 進と終端の NUL）。空文字列は「無し」。
inline constexpr uint32_t kGameProjectDecalAssetGuidCapacity = 33u;

enum class GameProjectDecalPatchField : uint32_t {
    None = 0u,
    Enabled = 1u << 0u,
    Size = 1u << 1u,
    BaseColorTexture = 1u << 2u,
    NormalTexture = 1u << 3u,
    Color = 1u << 4u,
    Opacity = 1u << 5u,
    // colorWeight / normalWeight / materialWeight.
    Weights = 1u << 6u,
    // roughness / metallic.
    Material = 1u << 7u,
    AngleFade = 1u << 8u,
    SortOrder = 1u << 9u,
};

constexpr GameProjectDecalPatchField operator|(GameProjectDecalPatchField lhs,
                                               GameProjectDecalPatchField rhs) noexcept {
    return static_cast<GameProjectDecalPatchField>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr uint32_t GameProjectDecalPatchFields(GameProjectDecalPatchField fields) noexcept {
    return static_cast<uint32_t>(fields);
}

inline constexpr uint32_t kGameProjectDecalPatchKnownFields = (1u << 10u) - 1u;

// Runtime snapshot of a DecalComponent. Values belong to the World instance and are not
// persisted back to the Scene document. Initialize size/version before reading.
struct GameProjectDecalState {
    uint32_t structSize = kGameProjectDecalStateV1Size;
    uint32_t version = kGameProjectDecalStateVersion;
    uint8_t enabled = 1u;
    uint8_t reserved0[3] = {0u, 0u, 0u};
    // Box width (local X), height (local Y) and projection depth (local Z) in metres.
    GameProjectFloat3 size{1.0f, 1.0f, 0.5f};
    GameProjectFloat3 color{1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    // How much colour, normal and roughness/metallic the decal applies (0..1, scaled by coverage x opacity).
    float colorWeight = 1.0f;
    float normalWeight = 1.0f;
    float materialWeight = 0.0f;
    float roughness = 0.5f;
    float metallic = 0.0f;
    // Surfaces turned further than this from the projection fade out, reaching zero at 90 degrees.
    float angleFadeDegrees = 60.0f;
    // Higher values draw over lower ones.
    int32_t sortOrder = 0;
    char baseColorTextureAssetGuid[kGameProjectDecalAssetGuidCapacity]{};
    char normalTextureAssetGuid[kGameProjectDecalAssetGuidCapacity]{};
    uint8_t reserved1[2] = {0u, 0u};
};

// Atomic runtime-only update. Only fields selected by fieldMask are read; values are clamped to the
// authored ranges. Texture GUIDs must be NUL-terminated asset GUIDs or empty to clear.
struct GameProjectDecalPatch {
    uint32_t structSize = kGameProjectDecalPatchV1Size;
    uint32_t version = kGameProjectDecalPatchVersion;
    uint32_t fieldMask = 0u;
    uint8_t enabled = 1u;
    uint8_t reserved0[3] = {0u, 0u, 0u};
    GameProjectFloat3 size{1.0f, 1.0f, 0.5f};
    GameProjectFloat3 color{1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    float colorWeight = 1.0f;
    float normalWeight = 1.0f;
    float materialWeight = 0.0f;
    float roughness = 0.5f;
    float metallic = 0.0f;
    float angleFadeDegrees = 60.0f;
    int32_t sortOrder = 0;
    char baseColorTextureAssetGuid[kGameProjectDecalAssetGuidCapacity]{};
    char normalTextureAssetGuid[kGameProjectDecalAssetGuidCapacity]{};
    uint8_t reserved1[2] = {0u, 0u};
};

static_assert(std::is_standard_layout_v<GameProjectDecalState>);
static_assert(std::is_trivially_copyable_v<GameProjectDecalState>);
static_assert(sizeof(GameProjectDecalState) == kGameProjectDecalStateV1Size);
static_assert(offsetof(GameProjectDecalState, size) == 12u);
static_assert(offsetof(GameProjectDecalState, sortOrder) == 64u);
static_assert(offsetof(GameProjectDecalState, baseColorTextureAssetGuid) == 68u);
static_assert(std::is_standard_layout_v<GameProjectDecalPatch>);
static_assert(std::is_trivially_copyable_v<GameProjectDecalPatch>);
static_assert(sizeof(GameProjectDecalPatch) == kGameProjectDecalPatchV1Size);
static_assert(offsetof(GameProjectDecalPatch, size) == 16u);
static_assert(offsetof(GameProjectDecalPatch, sortOrder) == 68u);
static_assert(offsetof(GameProjectDecalPatch, baseColorTextureAssetGuid) == 72u);

} // namespace HIKARI
