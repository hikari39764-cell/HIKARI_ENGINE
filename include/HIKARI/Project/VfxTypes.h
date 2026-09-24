#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "HIKARI/Project/ActionTypes.h"

namespace HIKARI {

// These fixed buffers are intentionally part of the public value contract.
// A GameProject module can keep an authored reference without crossing an
// allocator or an engine-owned string through the module boundary. Every
// populated field must contain a NUL within its declared capacity; the
// runtime rejects malformed or truncated buffers.
inline constexpr size_t kGameProjectVfxAssetGuidCapacity = 96u;
inline constexpr size_t kGameProjectVfxAssetPathCapacity = 512u;
inline constexpr size_t kGameProjectVfxInputIdCapacity = 96u;
inline constexpr size_t kGameProjectVfxProfileOutputIdCapacity = kGameProjectVfxInputIdCapacity;
inline constexpr uint32_t kGameProjectVfxMaximumParameterOverrides = 16u;

struct GameProjectVfxAssetReference {
    char assetGuid[kGameProjectVfxAssetGuidCapacity]{};
    char assetPath[kGameProjectVfxAssetPathCapacity]{};

    bool HasGuid() const noexcept { return assetGuid[0] != '\0'; }

    bool HasPath() const noexcept { return assetPath[0] != '\0'; }

    bool IsEmpty() const noexcept { return !HasGuid() && !HasPath(); }
};

// Alias kept in the same public tier so future non-VFX authored asset
// references can share the value shape without changing this VFX API.
using GameProjectVfxAssetId = GameProjectVfxAssetReference;

// A typed HVFX special-output reference.  The output id identifies the
// compiled profile inside the asset; the source path is retained only as
// an authoring fallback and is never a shipping identity.
struct GameProjectVfxProfileReference {
    GameProjectVfxAssetReference asset{};
    char outputId[kGameProjectVfxProfileOutputIdCapacity]{};

    bool HasOutputId() const noexcept { return outputId[0] != '\0'; }

    bool IsEmpty() const noexcept { return asset.IsEmpty() && !HasOutputId(); }
};

enum class GameProjectVfxLoopMode : uint8_t {
    UseAssetDefault = 0u,
    OneShot = 1u,
    Loop = 2u,
};

struct GameProjectVfxParameterOverride {
    char inputId[kGameProjectVfxInputIdCapacity]{};
    GameProjectBindingValue value{};
};

struct GameProjectVfxParameterOverrides {
    uint32_t count = 0u;
    GameProjectVfxParameterOverride values[kGameProjectVfxMaximumParameterOverrides]{};
};

struct GameProjectVfxPlayRequest {
    GameProjectVfxAssetReference asset{};
    uint64_t ownerObjectHandle = 0u;
    GameProjectTransform transform{};
    uint32_t randomSeed = 0u;
    GameProjectVfxLoopMode loopMode = GameProjectVfxLoopMode::UseAssetDefault;
    uint8_t reserved[3] = {0u, 0u, 0u};
    GameProjectVfxParameterOverrides parameters{};
};

struct GameProjectVfxUpdateRequest {
    // Parameter-only updates must not reset the instance transform.
    uint8_t updateTransform = 0u;
    uint8_t updateParameters = 0u;
    uint8_t reserved[2] = {0u, 0u};
    GameProjectTransform transform{};
    GameProjectVfxParameterOverrides parameters{};
};

// These are deliberately new request layouts.  The legacy presentation
// request structs have no size/version field and must remain unchanged
// for existing GameProject DLLs.
struct GameProjectWeaponTrailProfiledRequest {
    GameProjectVfxProfileReference profile{};
    GameProjectWeaponTrailSample sample{};
};

struct GameProjectPerfectGuardWaveProfiledRequest {
    GameProjectVfxProfileReference profile{};
    GameProjectPerfectGuardWaveRequest request{};
};

struct GameProjectAfterimageProfiledRequest {
    GameProjectVfxProfileReference profile{};
    GameProjectCombatWorldEffectRequest request{};
};

struct GameProjectVfxPlaybackHandle {
    static constexpr uint32_t kInvalidSlot = (std::numeric_limits<uint32_t>::max)();

    // The namespace prevents a handle from one World being reused in a
    // different VfxPlaybackService even when slot/generation coincide.
    uint64_t playbackNamespace = 0u;
    uint32_t slot = kInvalidSlot;
    uint32_t generation = 0u;

    bool IsValid() const noexcept { return playbackNamespace != 0u && slot != kInvalidSlot && generation != 0u; }
};

using GameProjectVfxInstanceHandle = GameProjectVfxPlaybackHandle;
using GameProjectVfxHandle = GameProjectVfxPlaybackHandle;

} // namespace HIKARI
