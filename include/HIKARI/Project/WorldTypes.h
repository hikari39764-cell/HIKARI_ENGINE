#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

    inline constexpr size_t kGameProjectObjectNameCapacity = 128u;

    enum class GameProjectWorldResultCode : uint8_t {
        Success = 0u,
        InvalidWorld = 1u,
        WrongThread = 2u,
        InvalidObject = 3u,
        InvalidParent = 4u,
        InvalidRequest = 5u,
        InvalidName = 6u,
        InvalidTransform = 7u,
        HierarchyCycle = 8u,
        MutationRejected = 9u,
        Unsupported = 10u,
        InternalError = 11u,
    };

    enum class GameProjectParentMode : uint8_t {
        KeepLocal = 0u,
        KeepWorld = 1u,
    };

    struct GameProjectWorldResult {
        GameProjectWorldResultCode code =
            GameProjectWorldResultCode::InternalError;
        GameProjectObjectHandle object{};
        uint32_t reserved = 0u;

        constexpr bool Succeeded() const noexcept {
            return code == GameProjectWorldResultCode::Success;
        }
    };

    struct GameProjectSpawnObjectDesc {
        char name[kGameProjectObjectNameCapacity]{};
        GameProjectTransform localTransform{};
        GameProjectObjectHandle parent{};
        uint32_t reserved[4] = { 0u, 0u, 0u, 0u };
    };

    static_assert(std::is_standard_layout_v<GameProjectObjectHandle>);
    static_assert(std::is_trivially_copyable_v<GameProjectObjectHandle>);
    static_assert(std::is_standard_layout_v<GameProjectTransform>);
    static_assert(std::is_trivially_copyable_v<GameProjectTransform>);
    static_assert(std::is_standard_layout_v<GameProjectWorldResult>);
    static_assert(std::is_trivially_copyable_v<GameProjectWorldResult>);
    static_assert(sizeof(GameProjectWorldResult) == 32u);
    static_assert(offsetof(GameProjectWorldResult, code) == 0u);
    static_assert(offsetof(GameProjectWorldResult, object) == 8u);
    static_assert(offsetof(GameProjectWorldResult, reserved) == 24u);
    static_assert(std::is_standard_layout_v<GameProjectSpawnObjectDesc>);
    static_assert(std::is_trivially_copyable_v<GameProjectSpawnObjectDesc>);
    static_assert(sizeof(GameProjectSpawnObjectDesc) == 200u);
    static_assert(offsetof(GameProjectSpawnObjectDesc, name) == 0u);
    static_assert(offsetof(GameProjectSpawnObjectDesc, localTransform) == 128u);
    static_assert(offsetof(GameProjectSpawnObjectDesc, parent) == 168u);
    static_assert(offsetof(GameProjectSpawnObjectDesc, reserved) == 184u);

} // namespace HIKARI
