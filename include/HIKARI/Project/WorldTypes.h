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
        Queued = 12u,
        CommandQueueFull = 13u,
        WorldClosing = 14u,
        CommandCanceled = 15u,
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

        constexpr bool Accepted() const noexcept {
            return Succeeded() ||
                code == GameProjectWorldResultCode::Queued;
        }

        constexpr bool IsQueued() const noexcept {
            return code == GameProjectWorldResultCode::Queued;
        }

        constexpr uint32_t GetCommandToken() const noexcept {
            // Correlation token for the current World command/report window;
            // it is not a persistent asset or globally unique identifier.
            return reserved;
        }
    };

    struct GameProjectSpawnObjectDesc {
        char name[kGameProjectObjectNameCapacity]{};
        GameProjectTransform localTransform{};
        GameProjectObjectHandle parent{};
        uint32_t reserved[4] = { 0u, 0u, 0u, 0u };
    };

    enum class GameProjectWorldCommandKind : uint8_t {
        Spawn = 0u,
        Destroy = 1u,
        SetParent = 2u,
    };

    enum class GameProjectWorldCommandPhase : uint8_t {
        Unknown = 0u,
        BeginFrame = 1u,
        ComponentUpdateBegin = 2u,
        ComponentUpdateEnd = 3u,
        QueryEnd = 4u,
        WorldClear = 5u,
        SystemAttach = 6u,
        SystemDetach = 7u,
        SystemPreUpdate = 8u,
        SystemPreFixedUpdate = 9u,
        SystemFixedUpdate = 10u,
        SystemPostFixedUpdate = 11u,
        SystemUpdate = 12u,
        SystemLateUpdate = 13u,
        SystemPreRender = 14u,
        SystemRender = 15u,
        SystemPostRender = 16u,
    };

    struct GameProjectWorldCommandFailure {
        uint32_t commandToken = 0u;
        GameProjectWorldCommandKind kind =
            GameProjectWorldCommandKind::Spawn;
        GameProjectWorldResultCode code =
            GameProjectWorldResultCode::InternalError;
        GameProjectWorldCommandPhase phase =
            GameProjectWorldCommandPhase::Unknown;
        uint8_t reserved = 0u;
        GameProjectObjectHandle object{};
        GameProjectObjectHandle parent{};
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
    static_assert(std::is_standard_layout_v<
        GameProjectWorldCommandFailure>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectWorldCommandFailure>);
    static_assert(sizeof(GameProjectWorldCommandFailure) == 40u);
    static_assert(offsetof(
        GameProjectWorldCommandFailure,
        commandToken) == 0u);
    static_assert(offsetof(
        GameProjectWorldCommandFailure,
        kind) == 4u);
    static_assert(offsetof(
        GameProjectWorldCommandFailure,
        code) == 5u);
    static_assert(offsetof(
        GameProjectWorldCommandFailure,
        phase) == 6u);
    static_assert(offsetof(
        GameProjectWorldCommandFailure,
        reserved) == 7u);
    static_assert(offsetof(
        GameProjectWorldCommandFailure,
        object) == 8u);
    static_assert(offsetof(
        GameProjectWorldCommandFailure,
        parent) == 24u);

} // namespace HIKARI
