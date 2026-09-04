#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ActionTypes.h"

namespace HIKARI {

    inline constexpr uint32_t kGameProjectSceneGuidCapacity = 33u;
    inline constexpr uint32_t kGameProjectSpawnPointIdCapacity = 64u;
    inline constexpr uint32_t kGameProjectDialogueAssetGuidCapacity = 33u;
    inline constexpr uint32_t kGameProjectDialogueNodeIdCapacity = 128u;
    inline constexpr uint32_t kGameProjectDialogueValueCapacity = 256u;
    inline constexpr uint32_t kGameProjectDialogueEventQueueCapacity = 64u;

    enum class GameProjectRuntimeHostKind : uint8_t {
        Unknown = 0u,
        StandaloneGame = 1u,
        EditorPreview = 2u,
    };

    enum class GameProjectDialogueEventType : uint32_t {
        Unknown = 0u,
        Started = 1u,
        NodeEntered = 2u,
        ChoiceSelected = 3u,
        EventEmitted = 4u,
        Ended = 5u,
    };

    enum class GameProjectDialogueEventFlags : uint32_t {
        None = 0u,
        AssetGuidTruncated = 1u << 0u,
        NodeIdTruncated = 1u << 1u,
        ValueTruncated = 1u << 2u,
    };

    constexpr GameProjectDialogueEventFlags operator|(
        GameProjectDialogueEventFlags lhs,
        GameProjectDialogueEventFlags rhs) noexcept {
        return static_cast<GameProjectDialogueEventFlags>(
            static_cast<uint32_t>(lhs) |
            static_cast<uint32_t>(rhs));
    }

    constexpr GameProjectDialogueEventFlags& operator|=(
        GameProjectDialogueEventFlags& lhs,
        GameProjectDialogueEventFlags rhs) noexcept {
        lhs = lhs | rhs;
        return lhs;
    }

    constexpr bool HasGameProjectDialogueEventFlag(
        GameProjectDialogueEventFlags value,
        GameProjectDialogueEventFlags flag) noexcept {
        return (static_cast<uint32_t>(value) &
            static_cast<uint32_t>(flag)) != 0u;
    }

    // World-owned dialogue events remain queued until consumed or until the
    // World is torn down. Text is always NUL-terminated UTF-8. A truncation
    // flag identifies every field whose complete value did not fit. value is
    // the selected choice id, emitted event id, or end reason for the
    // corresponding event type and is empty for Started/NodeEntered. sequence
    // starts at one per World dialogue service and saturates at uint64 max.
    struct GameProjectDialogueEvent {
        uint64_t sequence = 0u;
        uint64_t triggerObjectHandle = 0u;
        uint64_t interactorObjectHandle = 0u;
        GameProjectDialogueEventType type =
            GameProjectDialogueEventType::Unknown;
        GameProjectDialogueEventFlags flags =
            GameProjectDialogueEventFlags::None;
        char assetGuid[kGameProjectDialogueAssetGuidCapacity]{};
        char nodeId[kGameProjectDialogueNodeIdCapacity]{};
        char value[kGameProjectDialogueValueCapacity]{};
        uint8_t reserved[7]{ 0u, 0u, 0u, 0u, 0u, 0u, 0u };
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
    static_assert(sizeof(GameProjectDialogueEventType) == 4u);
    static_assert(sizeof(GameProjectDialogueEventFlags) == 4u);
    static_assert(std::is_standard_layout_v<GameProjectDialogueEvent>);
    static_assert(std::is_trivially_copyable_v<GameProjectDialogueEvent>);
    static_assert(offsetof(GameProjectDialogueEvent, sequence) == 0u);
    static_assert(offsetof(
        GameProjectDialogueEvent, triggerObjectHandle) == 8u);
    static_assert(offsetof(
        GameProjectDialogueEvent, interactorObjectHandle) == 16u);
    static_assert(offsetof(GameProjectDialogueEvent, type) == 24u);
    static_assert(offsetof(GameProjectDialogueEvent, flags) == 28u);
    static_assert(offsetof(GameProjectDialogueEvent, assetGuid) == 32u);
    static_assert(offsetof(GameProjectDialogueEvent, nodeId) == 65u);
    static_assert(offsetof(GameProjectDialogueEvent, value) == 193u);
    static_assert(offsetof(GameProjectDialogueEvent, reserved) == 449u);
    static_assert(sizeof(GameProjectDialogueEvent) == 456u);

} // namespace HIKARI
