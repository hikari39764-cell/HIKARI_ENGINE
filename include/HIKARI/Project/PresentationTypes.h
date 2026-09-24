#pragma once

#include <cstddef>
#include <cstdint>

#include "HIKARI/Project/ActionTypes.h"

namespace HIKARI {

// Fixed buffers are part of the public SDK contract. They keep authored
// model identities on the caller side of the module boundary without
// crossing an allocator or an engine-owned string.
inline constexpr size_t kGameProjectModelAssetGuidCapacity = 96u;
inline constexpr size_t kGameProjectModelAssetPathCapacity = 512u;
inline constexpr size_t kGameProjectPresentationSocketNameCapacity = 96u;

struct GameProjectModelAssetReference {
    char assetGuid[kGameProjectModelAssetGuidCapacity]{};
    char assetPath[kGameProjectModelAssetPathCapacity]{};

    bool HasGuid() const noexcept { return assetGuid[0] != '\0'; }

    bool HasPath() const noexcept { return assetPath[0] != '\0'; }

    bool IsEmpty() const noexcept { return !HasGuid() && !HasPath(); }
};

// When enabled, the render-only model is resolved from the owner's final
// animation pose and presentation world matrix during SceneRenderCache
// synchronization.  The worldTransform in the enclosing request remains
// the legacy/root-space fallback for non-attached submissions.
struct GameProjectPresentationModelAttachment {
    uint8_t enabled = 0u;
    uint8_t reserved[3] = {0u, 0u, 0u};
    char socketName[kGameProjectPresentationSocketNameCapacity]{};
    GameProjectTransform localTransform{};
};

// Runtime source selection for an existing ModelComponent. Transforms,
// visibility and render flags remain owned by the original object.
struct GameProjectModelAppearance {
    uint32_t structSize = sizeof(GameProjectModelAppearance);
    uint32_t version = 1u;
    char assetId[kGameProjectModelAssetPathCapacity]{};
    char materialSetAssetGuid[kGameProjectModelAssetGuidCapacity]{};
};

struct GameProjectPresentationModelRequest {
    uint64_t ownerObjectHandle = 0u;
    uint64_t instanceId = 0u;
    GameProjectModelAssetReference model{};
    GameProjectTransform worldTransform{};
    uint8_t visible = 1u;
    uint8_t castShadow = 1u;
    uint8_t receiveShadow = 1u;
    uint8_t reserved = 0u;
    // Empty selects imported glTF materials (or the existing model-level
    // default set when one is formally authored).  A non-empty value is
    // an exact-model Material Set GUID; invalid sets fail closed to the
    // imported material rather than selecting another default.
    char materialSetAssetGuid[kGameProjectModelAssetGuidCapacity]{};
    GameProjectPresentationModelAttachment attachment{};
};

} // namespace HIKARI
