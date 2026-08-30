#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "HIKARI/Project/ValueTypes.h"
#include "HIKARI/Runtime/ComponentTypeId.h"

namespace HIKARI {

    class IGameComponent;

    inline constexpr size_t kGameProjectObjectNameCapacity = 128u;
    inline constexpr size_t
        kGameProjectWorldMaximumSpawnBatchCount = 256u;
    inline constexpr uint32_t kGameProjectWorldQueryVersion = 1u;
    inline constexpr uint32_t kGameProjectWorldQueryMaximumTermCount = 8u;
    inline constexpr uint32_t kGameProjectWorldQueryTermV1Size = 24u;
    inline constexpr uint32_t kGameProjectWorldQueryDescV1Size = 56u;
    inline constexpr uint32_t kGameProjectWorldQueryRowV1Size = 48u;
    inline constexpr uint32_t kGameProjectWorldQueryResultV1Size = 32u;

    enum class GameProjectWorldQueryMatch : uint32_t {
        AllRequired = 0u,
        Count,
    };

    enum class GameProjectWorldQueryCardinality : uint32_t {
        AllEntries = 0u,
        FirstPerObject,
        Count,
    };

    enum class GameProjectWorldQueryAnchor : uint32_t {
        FirstRequiredTerm = 0u,
        SmallestUniqueBucket,
        Count,
    };

    enum class GameProjectWorldQueryOrder : uint32_t {
        AnchorAttachment = 0u,
        Count,
    };

    enum class GameProjectWorldQueryVisitResult : uint32_t {
        Continue = 0u,
        Stop,
        Error,
        Count,
    };

    enum class GameProjectWorldQueryResultCode : uint32_t {
        Success = 0u,
        Stopped,
        InvalidWorld,
        WrongThread,
        InvalidRequest,
        TooManyTerms,
        InvalidTerm,
        Unsupported,
        WorldClosing,
        CallbackError,
        InternalError,
    };

    // V1 term arrays have a fixed 24-byte stride; structSize must match V1.
    struct GameProjectWorldQueryTerm {
        uint32_t structSize = kGameProjectWorldQueryTermV1Size;
        uint32_t version = kGameProjectWorldQueryVersion;
        ComponentTypeId typeId{};
        uint32_t reserved[2] = { 0u, 0u };
    };

    // Terms are required and row components use the same term order. One-term
    // queries may visit AllEntries (including allowMultiple); multi-term v1
    // queries use FirstPerObject. SmallestUniqueBucket ties use term order.
    struct GameProjectWorldQueryDesc {
        uint32_t structSize = kGameProjectWorldQueryDescV1Size;
        uint32_t version = kGameProjectWorldQueryVersion;
        const GameProjectWorldQueryTerm* terms = nullptr;
        uint32_t termCount = 0u;
        GameProjectWorldQueryMatch match =
            GameProjectWorldQueryMatch::AllRequired;
        GameProjectWorldQueryCardinality cardinality =
            GameProjectWorldQueryCardinality::FirstPerObject;
        GameProjectWorldQueryAnchor anchor =
            GameProjectWorldQueryAnchor::SmallestUniqueBucket;
        GameProjectWorldQueryOrder order =
            GameProjectWorldQueryOrder::AnchorAttachment;
        uint32_t reserved[4] = { 0u, 0u, 0u, 0u };
    };

    // Callback-scoped borrowed row. Do not retain the row, component array, or
    // component pointers; the object handle itself may be copied and retained.
    struct GameProjectWorldQueryRow {
        uint32_t structSize = kGameProjectWorldQueryRowV1Size;
        uint32_t version = kGameProjectWorldQueryVersion;
        GameProjectObjectHandle object{};
        IGameComponent* const* components = nullptr;
        uint32_t componentCount = 0u;
        uint32_t reserved[3] = { 0u, 0u, 0u };
    };

    // rowsVisited counts delivered callback rows. QueryEnd command failures are
    // reported through WorldView's existing command-failure APIs, not here.
    struct GameProjectWorldQueryResult {
        uint32_t structSize = kGameProjectWorldQueryResultV1Size;
        uint32_t version = kGameProjectWorldQueryVersion;
        GameProjectWorldQueryResultCode code =
            GameProjectWorldQueryResultCode::InvalidRequest;
        GameProjectWorldQueryVisitResult callbackResult =
            GameProjectWorldQueryVisitResult::Continue;
        uint64_t rowsVisited = 0u;
        uint32_t diagnosticTermIndex =
            (std::numeric_limits<uint32_t>::max)();
        uint32_t reserved = 0u;

        constexpr bool Succeeded() const noexcept {
            return code == GameProjectWorldQueryResultCode::Success;
        }
    };

    using GameProjectWorldQueryCallback =
        GameProjectWorldQueryVisitResult (*)(
            void* context,
            const GameProjectWorldQueryRow* row) noexcept;

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
    static_assert(std::is_standard_layout_v<GameProjectWorldQueryTerm>);
    static_assert(std::is_same_v<std::underlying_type_t<
        GameProjectWorldQueryMatch>, uint32_t>);
    static_assert(std::is_same_v<std::underlying_type_t<
        GameProjectWorldQueryCardinality>, uint32_t>);
    static_assert(std::is_same_v<std::underlying_type_t<
        GameProjectWorldQueryAnchor>, uint32_t>);
    static_assert(std::is_same_v<std::underlying_type_t<
        GameProjectWorldQueryOrder>, uint32_t>);
    static_assert(std::is_same_v<std::underlying_type_t<
        GameProjectWorldQueryVisitResult>, uint32_t>);
    static_assert(std::is_same_v<std::underlying_type_t<
        GameProjectWorldQueryResultCode>, uint32_t>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectWorldQueryTerm>);
    static_assert(sizeof(GameProjectWorldQueryTerm) == 24u);
    static_assert(offsetof(GameProjectWorldQueryTerm, structSize) == 0u);
    static_assert(offsetof(GameProjectWorldQueryTerm, version) == 4u);
    static_assert(offsetof(GameProjectWorldQueryTerm, typeId) == 8u);
    static_assert(offsetof(GameProjectWorldQueryTerm, reserved) == 16u);
    static_assert(std::is_standard_layout_v<GameProjectWorldQueryDesc>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectWorldQueryDesc>);
    static_assert(sizeof(GameProjectWorldQueryDesc) == 56u);
    static_assert(offsetof(GameProjectWorldQueryDesc, structSize) == 0u);
    static_assert(offsetof(GameProjectWorldQueryDesc, version) == 4u);
    static_assert(offsetof(GameProjectWorldQueryDesc, terms) == 8u);
    static_assert(offsetof(GameProjectWorldQueryDesc, termCount) == 16u);
    static_assert(offsetof(GameProjectWorldQueryDesc, match) == 20u);
    static_assert(offsetof(GameProjectWorldQueryDesc, cardinality) == 24u);
    static_assert(offsetof(GameProjectWorldQueryDesc, anchor) == 28u);
    static_assert(offsetof(GameProjectWorldQueryDesc, order) == 32u);
    static_assert(offsetof(GameProjectWorldQueryDesc, reserved) == 36u);
    static_assert(std::is_standard_layout_v<GameProjectWorldQueryRow>);
    static_assert(std::is_trivially_copyable_v<GameProjectWorldQueryRow>);
    static_assert(sizeof(GameProjectWorldQueryRow) == 48u);
    static_assert(offsetof(GameProjectWorldQueryRow, structSize) == 0u);
    static_assert(offsetof(GameProjectWorldQueryRow, version) == 4u);
    static_assert(offsetof(GameProjectWorldQueryRow, object) == 8u);
    static_assert(offsetof(GameProjectWorldQueryRow, components) == 24u);
    static_assert(offsetof(GameProjectWorldQueryRow, componentCount) == 32u);
    static_assert(offsetof(GameProjectWorldQueryRow, reserved) == 36u);
    static_assert(std::is_standard_layout_v<GameProjectWorldQueryResult>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectWorldQueryResult>);
    static_assert(sizeof(GameProjectWorldQueryResult) == 32u);
    static_assert(offsetof(GameProjectWorldQueryResult, structSize) == 0u);
    static_assert(offsetof(GameProjectWorldQueryResult, version) == 4u);
    static_assert(offsetof(GameProjectWorldQueryResult, code) == 8u);
    static_assert(offsetof(
        GameProjectWorldQueryResult,
        callbackResult) == 12u);
    static_assert(offsetof(GameProjectWorldQueryResult, rowsVisited) == 16u);
    static_assert(offsetof(
        GameProjectWorldQueryResult,
        diagnosticTermIndex) == 24u);
    static_assert(offsetof(GameProjectWorldQueryResult, reserved) == 28u);
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
