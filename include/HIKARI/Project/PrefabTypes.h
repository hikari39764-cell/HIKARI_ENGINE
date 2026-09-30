#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "HIKARI/Project/WorldTypes.h"

namespace HIKARI {

inline constexpr uint32_t kGameProjectPrefabInstantiateOptionsVersion = 1u;
inline constexpr uint32_t kGameProjectPrefabInstantiateOptionsV1Size = 80u;
inline constexpr uint32_t kGameProjectPrefabRequestResultVersion = 1u;
inline constexpr uint32_t kGameProjectPrefabRequestResultV1Size = 72u;
inline constexpr uint32_t kGameProjectPrefabMaximumObjects = 256u;
inline constexpr uint32_t kGameProjectPrefabMaximumOutstandingRequests = 128u;
inline constexpr uint32_t kGameProjectPrefabMaximumInstances = 4096u;

struct GameProjectPrefabRequestHandle {
    GameProjectWorldHandle world{};
    uint64_t value = 0u;

    constexpr bool IsValid() const noexcept { return world.IsValid() && value != 0u; }

    friend constexpr bool operator==(GameProjectPrefabRequestHandle lhs, GameProjectPrefabRequestHandle rhs) noexcept {
        return lhs.world == rhs.world && lhs.value == rhs.value;
    }
};

struct GameProjectPrefabInstanceHandle {
    GameProjectWorldHandle world{};
    uint64_t value = 0u;

    constexpr bool IsValid() const noexcept { return world.IsValid() && value != 0u; }

    friend constexpr bool operator==(GameProjectPrefabInstanceHandle lhs,
                                     GameProjectPrefabInstanceHandle rhs) noexcept {
        return lhs.world == rhs.world && lhs.value == rhs.value;
    }
};

enum class GameProjectPrefabRequestState : uint32_t {
    Invalid = 0u,
    Pending,
    Succeeded,
    Failed,
    Cancelled,
};

enum class GameProjectPrefabError : uint32_t {
    None = 0u,
    InvalidRequest,
    InvalidAsset,
    Dependency,
    InvalidParent,
    UnsupportedComponent,
    InitializationFailed,
    CapacityExceeded,
    Cancelled,
    InternalError,
};

// rootLocalTransform replaces the Prefab root's authored local transform
// when overrideRootTransform is nonzero. It is local to parent, not world-space.
struct GameProjectPrefabInstantiateOptions {
    uint32_t structSize = kGameProjectPrefabInstantiateOptionsV1Size;
    uint32_t version = kGameProjectPrefabInstantiateOptionsVersion;
    GameProjectObjectHandle parent{};
    GameProjectTransform rootLocalTransform{};
    uint32_t overrideRootTransform = 0u;
    uint32_t reserved[3] = {0u, 0u, 0u};
};

struct GameProjectPrefabSubmission {
    GameProjectPrefabRequestHandle request{};
    GameProjectPrefabError error = GameProjectPrefabError::None;

    constexpr bool Accepted() const noexcept { return request.IsValid() && error == GameProjectPrefabError::None; }
};

struct GameProjectPrefabRequestResult {
    uint32_t structSize = kGameProjectPrefabRequestResultV1Size;
    uint32_t version = kGameProjectPrefabRequestResultVersion;
    GameProjectPrefabRequestState state = GameProjectPrefabRequestState::Invalid;
    GameProjectPrefabError error = GameProjectPrefabError::None;
    GameProjectPrefabInstanceHandle instance{};
    GameProjectObjectHandle rootObject{};
    uint32_t objectCount = 0u;
    uint32_t reserved[4] = {0u, 0u, 0u, 0u};
};

static_assert(std::is_standard_layout_v<GameProjectPrefabInstantiateOptions>);
static_assert(std::is_trivially_copyable_v<GameProjectPrefabInstantiateOptions>);
static_assert(sizeof(GameProjectPrefabInstantiateOptions) == kGameProjectPrefabInstantiateOptionsV1Size);
static_assert(std::is_standard_layout_v<GameProjectPrefabSubmission>);
static_assert(std::is_trivially_copyable_v<GameProjectPrefabSubmission>);
static_assert(std::is_standard_layout_v<GameProjectPrefabRequestResult>);
static_assert(std::is_trivially_copyable_v<GameProjectPrefabRequestResult>);
static_assert(sizeof(GameProjectPrefabRequestResult) == kGameProjectPrefabRequestResultV1Size);

} // namespace HIKARI
