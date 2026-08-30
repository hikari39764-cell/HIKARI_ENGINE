#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/WorldMassTypes.h"

namespace HIKARI {

    class IGameSystem;
    struct FrameContext;

    inline constexpr uint32_t kGameProjectMassSystemVersion = 1u;
    inline constexpr uint32_t kGameProjectMassSystemAccessDescV1Size = 32u;
    inline constexpr uint32_t kGameProjectMassSystemScheduleDescV1Size = 72u;
    inline constexpr uint32_t kGameProjectMassPartitionInputV1Size = 120u;
    inline constexpr uint32_t kGameProjectMassPartitionOutputV1Size = 80u;
    inline constexpr uint32_t kGameProjectMassCommandWriterV1Size = 32u;
    inline constexpr uint32_t kGameProjectMassSystemMinimumPartitionSize = 256u;
    inline constexpr uint32_t kGameProjectMassSystemMaximumPartitionSize = 4096u;
    inline constexpr uint32_t
        kGameProjectMassSystemMaximumDestroyCommandsPerPartition = 256u;

    enum class GameProjectMassSystemPhase : uint32_t {
        PreUpdate = 0u,
        PreFixedUpdate,
        FixedUpdate,
        PostFixedUpdate,
        Update,
        LateUpdate,
        Count,
    };

    enum class GameProjectMassSystemField : uint32_t {
        None = 0u,
        Transform = 1u << 0u,
        Velocity = 1u << 1u,
        ProjectData = 1u << 2u,
    };

    constexpr GameProjectMassSystemField operator|(
        GameProjectMassSystemField lhs,
        GameProjectMassSystemField rhs) noexcept {
        return static_cast<GameProjectMassSystemField>(
            static_cast<uint32_t>(lhs) |
            static_cast<uint32_t>(rhs));
    }

    enum class GameProjectMassSystemFlags : uint32_t {
        None = 0u,
        AllowDestroy = 1u << 0u,
    };

    struct GameProjectMassSystemAccessDesc {
        uint32_t structSize = kGameProjectMassSystemAccessDescV1Size;
        uint32_t version = kGameProjectMassSystemVersion;
        ComponentTypeId typeId{};
        uint32_t readMask = 0u;
        uint32_t writeMask = 0u;
        uint32_t flags = 0u;
        uint32_t reserved = 0u;
    };

    struct GameProjectMassSystemScheduleDesc {
        uint32_t structSize = kGameProjectMassSystemScheduleDescV1Size;
        uint32_t version = kGameProjectMassSystemVersion;
        GameProjectMassSystemAccessDesc access{};
        GameProjectMassSystemPhase phase =
            GameProjectMassSystemPhase::Update;
        uint32_t partitionSize = 1024u;
        uint32_t maximumDestroyCommandsPerPartition = 0u;
        uint32_t flags = 0u;
        uint32_t reserved[3] = { 0u, 0u, 0u };
    };

    // Engine-owned immutable value snapshot. All arrays are borrowed only for
    // one partition callback and may be null when the field was not declared.
    struct GameProjectMassPartitionInput {
        uint32_t structSize = kGameProjectMassPartitionInputV1Size;
        uint32_t version = kGameProjectMassSystemVersion;
        GameProjectWorldHandle world{};
        ComponentTypeId typeId{};
        uint64_t layoutHash = 0u;
        uint32_t schemaVersion = 0u;
        uint32_t firstRow = 0u;
        uint32_t rowCount = 0u;
        uint32_t podStride = 0u;
        uint32_t podSize = 0u;
        uint32_t podAlignment = 0u;
        uint32_t readMask = 0u;
        uint32_t writeMask = 0u;
        const GameProjectObjectHandle* objects = nullptr;
        const GameProjectFloat3* positions = nullptr;
        const GameProjectQuaternion* rotations = nullptr;
        const GameProjectFloat3* scales = nullptr;
        const GameProjectFloat3* velocities = nullptr;
        const void* podData = nullptr;
        uint32_t reserved[2] = { 0u, 0u };
    };

    // Host-owned writeback scratch initialized from the input snapshot. All
    // pointers are borrowed only for this callback. The owner thread applies
    // them only after every partition succeeds.
    struct GameProjectMassPartitionOutput {
        uint32_t structSize = kGameProjectMassPartitionOutputV1Size;
        uint32_t version = kGameProjectMassSystemVersion;
        uint32_t firstRow = 0u;
        uint32_t rowCount = 0u;
        uint32_t writeMask = 0u;
        uint32_t reserved0 = 0u;
        GameProjectFloat3* positions = nullptr;
        GameProjectQuaternion* rotations = nullptr;
        GameProjectFloat3* scales = nullptr;
        GameProjectFloat3* velocities = nullptr;
        void* podData = nullptr;
        uint64_t podDataByteCount = 0u;
        uint32_t reserved[2] = { 0u, 0u };
    };

    using GameProjectMassDestroyRowFn = GameProjectWorldResult (*)(
        void* context,
        uint32_t partitionRow) noexcept;

    // Callback-scoped command writer. Do not retain the writer, its context or
    // function pointer after ExecuteMassPartition returns.
    struct GameProjectMassCommandWriter {
        uint32_t structSize = kGameProjectMassCommandWriterV1Size;
        uint32_t version = kGameProjectMassSystemVersion;
        void* context = nullptr;
        GameProjectMassDestroyRowFn destroyRow = nullptr;
        uint32_t capacity = 0u;
        uint32_t reserved = 0u;

        GameProjectWorldResult DestroyRow(
            uint32_t partitionRow) const noexcept {
            return destroyRow != nullptr
                ? destroyRow(context, partitionRow)
                : GameProjectWorldResult{
                    GameProjectWorldResultCode::Unsupported,
                    {},
                    0u
                };
        }
    };

    // Partition kernels may write only their output/writer. Shared reductions
    // must be emitted for deterministic owner-thread merge, not accumulated in
    // completion order through mutable system or external state.
    using ExecuteGameProjectMassPartitionFn = bool (*)(
        const IGameSystem* system,
        const GameProjectMassPartitionInput* input,
        GameProjectMassPartitionOutput* output,
        const GameProjectMassCommandWriter* commands,
        const FrameContext* frame) noexcept;

    static_assert(std::is_standard_layout_v<
        GameProjectMassSystemAccessDesc>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectMassSystemAccessDesc>);
    static_assert(std::is_standard_layout_v<
        GameProjectMassSystemScheduleDesc>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectMassSystemScheduleDesc>);
    static_assert(std::is_standard_layout_v<
        GameProjectMassPartitionInput>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectMassPartitionInput>);
    static_assert(std::is_standard_layout_v<
        GameProjectMassPartitionOutput>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectMassPartitionOutput>);
    static_assert(std::is_standard_layout_v<
        GameProjectMassCommandWriter>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectMassCommandWriter>);
    static_assert(sizeof(GameProjectMassSystemAccessDesc) == 32u);
    static_assert(offsetof(GameProjectMassSystemAccessDesc, typeId) == 8u);
    static_assert(offsetof(GameProjectMassSystemAccessDesc, readMask) == 16u);
    static_assert(sizeof(GameProjectMassSystemScheduleDesc) == 72u);
    static_assert(offsetof(GameProjectMassSystemScheduleDesc, access) == 8u);
    static_assert(offsetof(GameProjectMassSystemScheduleDesc, phase) == 40u);
    static_assert(sizeof(GameProjectMassPartitionInput) == 120u);
    static_assert(offsetof(GameProjectMassPartitionInput, world) == 8u);
    static_assert(offsetof(GameProjectMassPartitionInput, objects) == 64u);
    static_assert(offsetof(GameProjectMassPartitionInput, podData) == 104u);
    static_assert(sizeof(GameProjectMassPartitionOutput) == 80u);
    static_assert(offsetof(GameProjectMassPartitionOutput, positions) == 24u);
    static_assert(offsetof(GameProjectMassPartitionOutput, podData) == 56u);
    static_assert(sizeof(GameProjectMassCommandWriter) == 32u);
    static_assert(offsetof(GameProjectMassCommandWriter, context) == 8u);
    static_assert(offsetof(GameProjectMassCommandWriter, destroyRow) == 16u);

} // namespace HIKARI
