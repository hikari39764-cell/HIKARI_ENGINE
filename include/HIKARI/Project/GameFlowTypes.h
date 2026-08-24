#pragma once

#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ActionTypes.h"

namespace HIKARI {

    inline constexpr uint32_t kGameProjectSceneGuidCapacity = 33u;
    inline constexpr uint32_t kGameProjectSpawnPointIdCapacity = 64u;

    enum class GameProjectRuntimeHostKind : uint8_t {
        Unknown = 0u,
        StandaloneGame = 1u,
        EditorPreview = 2u,
    };

    // A scene-entry token survives the old World being detached. The opaque
    // userData value belongs to the GameProject and is never interpreted by
    // the engine. Runtime handles are intentionally absent.
    struct GameProjectSceneEntry {
        uint64_t userData = 0u;
        GameProjectFloat3 spawnPosition{};
        GameProjectQuat spawnRotation{};
        char sceneAssetGuid[kGameProjectSceneGuidCapacity]{};
        char spawnPointId[kGameProjectSpawnPointIdCapacity]{};
        uint8_t hasSpawnTransform = 0u;
        uint8_t reserved[7] = { 0u, 0u, 0u, 0u, 0u, 0u, 0u };
    };

    static_assert(std::is_standard_layout_v<GameProjectSceneEntry>);
    static_assert(std::is_trivially_copyable_v<GameProjectSceneEntry>);

} // namespace HIKARI
