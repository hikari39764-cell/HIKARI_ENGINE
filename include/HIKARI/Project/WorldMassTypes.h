#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/WorldTypes.h"

namespace HIKARI {

    inline constexpr uint32_t kGameProjectWorldMassVersion = 1u;
    inline constexpr uint32_t kGameProjectMassDataTypeDescV1Size = 40u;
    inline constexpr uint32_t
        kGameProjectWorldMassSpawnBatchDescV1Size = 72u;
    inline constexpr uint32_t kGameProjectWorldMassRowV1Size = 80u;
    inline constexpr uint32_t
        kGameProjectWorldMaximumMassSpawnBatchCount = 1024u;
    inline constexpr uint32_t kGameProjectWorldMaximumMassPodBytes = 1024u;

    struct GameProjectMassDataTypeDesc {
        uint32_t structSize = kGameProjectMassDataTypeDescV1Size;
        uint32_t version = kGameProjectWorldMassVersion;
        ComponentTypeId typeId{};
        uint64_t layoutHash = 0u;
        uint32_t schemaVersion = 1u;
        uint32_t dataSize = 0u;
        uint32_t dataAlignment = 0u;
        uint32_t reserved = 0u;
    };

    // All pointed-to arrays are borrowed only for SpawnMassObjects. POD rows
    // use podStride bytes and podDataByteCount closes the source bounds.
    struct GameProjectWorldMassSpawnBatchDesc {
        uint32_t structSize =
            kGameProjectWorldMassSpawnBatchDescV1Size;
        uint32_t version = kGameProjectWorldMassVersion;
        ComponentTypeId typeId{};
        const GameProjectTransform* transforms = nullptr;
        const GameProjectFloat3* velocities = nullptr;
        const void* podData = nullptr;
        uint64_t podDataByteCount = 0u;
        uint32_t objectCount = 0u;
        uint32_t podStride = 0u;
        uint32_t reserved[3] = { 0u, 0u, 0u };
    };

    // Callback-scoped mutable row. The pointers and POD bytes must not be
    // retained; the object handle may be copied and retained.
    struct GameProjectWorldMassRow {
        uint32_t structSize = kGameProjectWorldMassRowV1Size;
        uint32_t version = kGameProjectWorldMassVersion;
        GameProjectObjectHandle object{};
        GameProjectFloat3* position = nullptr;
        GameProjectQuaternion* rotation = nullptr;
        GameProjectFloat3* scale = nullptr;
        GameProjectFloat3* velocity = nullptr;
        void* podData = nullptr;
        uint32_t podSize = 0u;
        uint32_t podAlignment = 0u;
        uint32_t reserved[2] = { 0u, 0u };
    };

    using GameProjectWorldMassCallback =
        GameProjectWorldQueryVisitResult (*)(
            void* context,
            GameProjectWorldMassRow* row) noexcept;

    static_assert(std::is_standard_layout_v<GameProjectMassDataTypeDesc>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectMassDataTypeDesc>);
    static_assert(sizeof(GameProjectMassDataTypeDesc) == 40u);
    static_assert(offsetof(GameProjectMassDataTypeDesc, typeId) == 8u);
    static_assert(offsetof(
        GameProjectMassDataTypeDesc, layoutHash) == 16u);
    static_assert(offsetof(
        GameProjectMassDataTypeDesc, schemaVersion) == 24u);
    static_assert(offsetof(GameProjectMassDataTypeDesc, dataSize) == 28u);
    static_assert(offsetof(
        GameProjectMassDataTypeDesc, dataAlignment) == 32u);
    static_assert(std::is_standard_layout_v<
        GameProjectWorldMassSpawnBatchDesc>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectWorldMassSpawnBatchDesc>);
    static_assert(sizeof(GameProjectWorldMassSpawnBatchDesc) == 72u);
    static_assert(offsetof(
        GameProjectWorldMassSpawnBatchDesc, typeId) == 8u);
    static_assert(offsetof(
        GameProjectWorldMassSpawnBatchDesc, transforms) == 16u);
    static_assert(offsetof(
        GameProjectWorldMassSpawnBatchDesc, velocities) == 24u);
    static_assert(offsetof(
        GameProjectWorldMassSpawnBatchDesc, podData) == 32u);
    static_assert(offsetof(
        GameProjectWorldMassSpawnBatchDesc, podDataByteCount) == 40u);
    static_assert(offsetof(
        GameProjectWorldMassSpawnBatchDesc, objectCount) == 48u);
    static_assert(offsetof(
        GameProjectWorldMassSpawnBatchDesc, podStride) == 52u);
    static_assert(std::is_standard_layout_v<GameProjectWorldMassRow>);
    static_assert(std::is_trivially_copyable_v<GameProjectWorldMassRow>);
    static_assert(sizeof(GameProjectWorldMassRow) == 80u);
    static_assert(offsetof(GameProjectWorldMassRow, object) == 8u);
    static_assert(offsetof(GameProjectWorldMassRow, position) == 24u);
    static_assert(offsetof(GameProjectWorldMassRow, rotation) == 32u);
    static_assert(offsetof(GameProjectWorldMassRow, scale) == 40u);
    static_assert(offsetof(GameProjectWorldMassRow, velocity) == 48u);
    static_assert(offsetof(GameProjectWorldMassRow, podData) == 56u);
    static_assert(offsetof(GameProjectWorldMassRow, podSize) == 64u);
    static_assert(offsetof(GameProjectWorldMassRow, podAlignment) == 68u);

} // namespace HIKARI
