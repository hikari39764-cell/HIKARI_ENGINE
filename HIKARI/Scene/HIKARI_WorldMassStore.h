#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "HIKARI/Project/ValueTypes.h"
#include "HIKARI/Runtime/ComponentTypeId.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {

    inline constexpr uint32_t kWorldMassMaximumPodBytes = 1024u;

    struct WorldMassSchema {
        ComponentTypeId typeId{};
        uint64_t layoutHash = 0u;
        uint32_t schemaVersion = 0u;
        uint32_t podSize = 0u;
        uint32_t podAlignment = 0u;
    };

    bool IsValidWorldMassSchema(
        const WorldMassSchema& schema) noexcept;
    bool IsSameWorldMassSchema(
        const WorldMassSchema& lhs,
        const WorldMassSchema& rhs) noexcept;

    class WorldMassSchemaRegistry {
    public:
        bool Register(WorldMassSchema schema) noexcept;
        void Clear() noexcept;
        const WorldMassSchema* Find(
            ComponentTypeId typeId) const noexcept;

    private:
        std::unordered_map<uint64_t, WorldMassSchema> schemas_{};
    };

    struct WorldMassSpawnBatchRequest {
        WorldMassSchema schema{};
        std::span<const GameProjectTransform> transforms{};
        std::span<const GameProjectFloat3> velocities{};
        const std::byte* podData = nullptr;
        uint32_t podStride = 0u;
        size_t podDataByteCount = 0u;
    };

    struct WorldMassMutableRow {
        RuntimeObjectHandle object{};
        GameProjectFloat3* position = nullptr;
        GameProjectQuaternion* rotation = nullptr;
        GameProjectFloat3* scale = nullptr;
        GameProjectFloat3* velocity = nullptr;
        std::byte* podData = nullptr;
        uint32_t podSize = 0u;
        uint32_t podAlignment = 0u;
    };

    class WorldMassStore {
    public:
        using MutableVisitorFn = bool (*)(
            void* context,
            size_t rowIndex,
            WorldMassMutableRow& row) noexcept;

        bool CanUseSchema(const WorldMassSchema& schema) const noexcept;
        bool AppendBatch(
            const WorldMassSchema& schema,
            std::span<const RuntimeObjectHandle> objects,
            std::span<const GameProjectTransform> transforms,
            std::span<const GameProjectFloat3> velocities,
            const std::byte* podData,
            size_t podStride,
            std::span<const uint32_t> sourceIndices,
            size_t& outFirstRowIndex);
        bool RemoveBackBatch(
            const WorldMassSchema& schema,
            size_t firstRowIndex,
            std::span<const RuntimeObjectHandle> objects) noexcept;
        bool Remove(
            ComponentTypeId typeId,
            size_t rowIndex,
            RuntimeObjectHandle object,
            RuntimeObjectHandle& outMovedObject) noexcept;
        bool TryGetMutableRow(
            ComponentTypeId typeId,
            size_t rowIndex,
            WorldMassMutableRow& outRow) noexcept;
        bool VisitMutable(
            ComponentTypeId typeId,
            void* context,
            MutableVisitorFn visitor) noexcept;
        bool TryGetTransform(
            ComponentTypeId typeId,
            size_t rowIndex,
            RuntimeObjectHandle object,
            GameProjectTransform& outTransform) const noexcept;
        bool SetTransform(
            ComponentTypeId typeId,
            size_t rowIndex,
            RuntimeObjectHandle object,
            const GameProjectTransform& transform) noexcept;
        size_t GetCount(ComponentTypeId typeId) const noexcept;
        size_t GetTotalCount() const noexcept;
        void Clear() noexcept;

    private:
        struct Store {
            WorldMassSchema schema{};
            size_t podStride = 0u;
            std::vector<RuntimeObjectHandle> objects{};
            std::vector<GameProjectFloat3> positions{};
            std::vector<GameProjectQuaternion> rotations{};
            std::vector<GameProjectFloat3> scales{};
            std::vector<GameProjectFloat3> velocities{};
            std::vector<std::byte> podBytes{};
        };

        bool RemoveFromStore(
            Store& store,
            size_t rowIndex,
            RuntimeObjectHandle object,
            RuntimeObjectHandle& outMovedObject) noexcept;
        static bool TryGetTransformFromStore(
            const Store& store,
            size_t rowIndex,
            RuntimeObjectHandle object,
            GameProjectTransform& outTransform) noexcept;
        static bool SetTransformInStore(
            Store& store,
            size_t rowIndex,
            RuntimeObjectHandle object,
            const GameProjectTransform& transform) noexcept;
        static size_t AlignUp(size_t value, size_t alignment) noexcept;
        Store* FindStore(ComponentTypeId typeId) noexcept;
        const Store* FindStore(ComponentTypeId typeId) const noexcept;

        std::unordered_map<uint64_t, Store> stores_{};
        size_t totalCount_ = 0u;
    };

} // namespace HIKARI
