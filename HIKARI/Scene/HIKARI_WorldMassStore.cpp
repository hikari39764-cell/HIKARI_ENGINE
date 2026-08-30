#include "Scene/HIKARI_WorldMassStore.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "Scene/Internal/HIKARI_WorldCapacity.h"

namespace HIKARI {

    bool IsValidWorldMassSchema(
        const WorldMassSchema& schema) noexcept {
        return schema.typeId.IsValid() &&
            schema.layoutHash != 0u &&
            schema.schemaVersion != 0u &&
            schema.podSize != 0u &&
            schema.podSize <= kWorldMassMaximumPodBytes &&
            schema.podAlignment != 0u &&
            schema.podAlignment <= alignof(std::max_align_t) &&
            (schema.podAlignment & (schema.podAlignment - 1u)) == 0u;
    }

    bool IsSameWorldMassSchema(
        const WorldMassSchema& lhs,
        const WorldMassSchema& rhs) noexcept {
        return lhs.typeId == rhs.typeId &&
            lhs.layoutHash == rhs.layoutHash &&
            lhs.schemaVersion == rhs.schemaVersion &&
            lhs.podSize == rhs.podSize &&
            lhs.podAlignment == rhs.podAlignment;
    }

    bool WorldMassSchemaRegistry::Register(
        WorldMassSchema schema) noexcept {
        if (!IsValidWorldMassSchema(schema)) {
            return false;
        }
        try {
            return schemas_.emplace(schema.typeId.value, schema).second;
        }
        catch (...) {
            return false;
        }
    }

    void WorldMassSchemaRegistry::Clear() noexcept {
        schemas_.clear();
    }

    const WorldMassSchema* WorldMassSchemaRegistry::Find(
        ComponentTypeId typeId) const noexcept {
        if (!typeId.IsValid()) {
            return nullptr;
        }
        const auto found = schemas_.find(typeId.value);
        return found != schemas_.end() ? &found->second : nullptr;
    }

    size_t WorldMassStore::AlignUp(
        size_t value,
        size_t alignment) noexcept {
        return (value + alignment - 1u) & ~(alignment - 1u);
    }

    WorldMassStore::Store* WorldMassStore::FindStore(
        ComponentTypeId typeId) noexcept {
        const auto found = stores_.find(typeId.value);
        return found != stores_.end() ? &found->second : nullptr;
    }

    const WorldMassStore::Store* WorldMassStore::FindStore(
        ComponentTypeId typeId) const noexcept {
        return const_cast<WorldMassStore*>(this)->FindStore(typeId);
    }

    bool WorldMassStore::CanUseSchema(
        const WorldMassSchema& schema) const noexcept {
        if (!IsValidWorldMassSchema(schema)) {
            return false;
        }
        const Store* store = FindStore(schema.typeId);
        return store == nullptr ||
            IsSameWorldMassSchema(store->schema, schema);
    }

    bool WorldMassStore::AppendBatch(
        const WorldMassSchema& schema,
        std::span<const RuntimeObjectHandle> objects,
        std::span<const GameProjectTransform> transforms,
        std::span<const GameProjectFloat3> velocities,
        const std::byte* podData,
        size_t podStride,
        std::span<const uint32_t> sourceIndices,
        size_t& outFirstRowIndex) {
        outFirstRowIndex = (std::numeric_limits<size_t>::max)();
        if (objects.empty() || objects.size() != sourceIndices.size() ||
            transforms.size() != velocities.size() ||
            podData == nullptr || podStride < schema.podSize ||
            !IsValidWorldMassSchema(schema)) {
            return false;
        }
        for (size_t index = 0u; index < sourceIndices.size(); ++index) {
            if (!objects[index].IsValid() ||
                sourceIndices[index] >= transforms.size()) {
                return false;
            }
        }

        auto [entry, inserted] = stores_.try_emplace(schema.typeId.value);
        Store& store = entry->second;
        if (inserted) {
            store.schema = schema;
            store.podStride = AlignUp(
                schema.podSize,
                schema.podAlignment);
        }
        else if (!IsSameWorldMassSchema(store.schema, schema)) {
            return false;
        }
        if (objects.size() > store.objects.max_size() -
            store.objects.size()) {
            return false;
        }
        const size_t nextCount = store.objects.size() + objects.size();
        const size_t capacity = INTERNAL::GrowWorldCapacity(
            store.objects.capacity(),
            nextCount,
            (std::min)({
                store.objects.max_size(),
                store.positions.max_size(),
                store.rotations.max_size(),
                store.scales.max_size(),
                store.velocities.max_size()
            }));
        if (capacity >
            store.podBytes.max_size() / store.podStride) {
            return false;
        }
        store.objects.reserve(capacity);
        store.positions.reserve(capacity);
        store.rotations.reserve(capacity);
        store.scales.reserve(capacity);
        store.velocities.reserve(capacity);
        store.podBytes.reserve(capacity * store.podStride);

        outFirstRowIndex = store.objects.size();
        const size_t firstByteOffset =
            outFirstRowIndex * store.podStride;
        store.podBytes.resize(nextCount * store.podStride);
        for (size_t index = 0u; index < objects.size(); ++index) {
            const size_t sourceIndex = sourceIndices[index];
            store.objects.push_back(objects[index]);
            store.positions.push_back(transforms[sourceIndex].position);
            store.rotations.push_back(transforms[sourceIndex].rotation);
            store.scales.push_back(transforms[sourceIndex].scale);
            store.velocities.push_back(velocities[sourceIndex]);
            const size_t targetByteOffset =
                firstByteOffset + index * store.podStride;
            std::memcpy(
                store.podBytes.data() + targetByteOffset,
                podData + sourceIndex * podStride,
                schema.podSize);
        }
        totalCount_ += objects.size();
        return true;
    }

    bool WorldMassStore::RemoveBackBatch(
        const WorldMassSchema& schema,
        size_t firstRowIndex,
        std::span<const RuntimeObjectHandle> objects) noexcept {
        Store* store = FindStore(schema.typeId);
        if (store == nullptr ||
            !IsSameWorldMassSchema(store->schema, schema) ||
            firstRowIndex > store->objects.size() ||
            objects.size() != store->objects.size() - firstRowIndex) {
            return false;
        }
        for (size_t index = 0u; index < objects.size(); ++index) {
            if (store->objects[firstRowIndex + index] != objects[index]) {
                return false;
            }
        }
        store->objects.resize(firstRowIndex);
        store->positions.resize(firstRowIndex);
        store->rotations.resize(firstRowIndex);
        store->scales.resize(firstRowIndex);
        store->velocities.resize(firstRowIndex);
        store->podBytes.resize(firstRowIndex * store->podStride);
        totalCount_ -= objects.size();
        return true;
    }

    bool WorldMassStore::RemoveFromStore(
        Store& store,
        size_t rowIndex,
        RuntimeObjectHandle object,
        RuntimeObjectHandle& outMovedObject) noexcept {
        outMovedObject = {};
        if (rowIndex >= store.objects.size() ||
            store.objects[rowIndex] != object) {
            return false;
        }
        const size_t lastIndex = store.objects.size() - 1u;
        if (rowIndex != lastIndex) {
            outMovedObject = store.objects[lastIndex];
            store.objects[rowIndex] = outMovedObject;
            store.positions[rowIndex] = store.positions[lastIndex];
            store.rotations[rowIndex] = store.rotations[lastIndex];
            store.scales[rowIndex] = store.scales[lastIndex];
            store.velocities[rowIndex] = store.velocities[lastIndex];
            std::memmove(
                store.podBytes.data() + rowIndex * store.podStride,
                store.podBytes.data() + lastIndex * store.podStride,
                store.podStride);
        }
        store.objects.pop_back();
        store.positions.pop_back();
        store.rotations.pop_back();
        store.scales.pop_back();
        store.velocities.pop_back();
        store.podBytes.resize(lastIndex * store.podStride);
        --totalCount_;
        return true;
    }

    bool WorldMassStore::Remove(
        ComponentTypeId typeId,
        size_t rowIndex,
        RuntimeObjectHandle object,
        RuntimeObjectHandle& outMovedObject) noexcept {
        Store* store = FindStore(typeId);
        return store != nullptr && RemoveFromStore(
            *store,
            rowIndex,
            object,
            outMovedObject);
    }

    bool WorldMassStore::VisitMutable(
        ComponentTypeId typeId,
        void* context,
        MutableVisitorFn visitor) noexcept {
        Store* store = FindStore(typeId);
        if (store == nullptr) {
            return true;
        }
        if (visitor == nullptr) {
            return false;
        }
        for (size_t rowIndex = 0u;
            rowIndex < store->objects.size();
            ++rowIndex) {
            WorldMassMutableRow row{
                store->objects[rowIndex],
                &store->positions[rowIndex],
                &store->rotations[rowIndex],
                &store->scales[rowIndex],
                &store->velocities[rowIndex],
                store->podBytes.data() + rowIndex * store->podStride,
                store->schema.podSize,
                store->schema.podAlignment
            };
            if (!visitor(context, rowIndex, row)) {
                return false;
            }
        }
        return true;
    }

    bool WorldMassStore::TryGetMutableRow(
        ComponentTypeId typeId,
        size_t rowIndex,
        WorldMassMutableRow& outRow) noexcept {
        outRow = {};
        Store* store = FindStore(typeId);
        if (store == nullptr || rowIndex >= store->objects.size()) {
            return false;
        }
        outRow.object = store->objects[rowIndex];
        outRow.position = &store->positions[rowIndex];
        outRow.rotation = &store->rotations[rowIndex];
        outRow.scale = &store->scales[rowIndex];
        outRow.velocity = &store->velocities[rowIndex];
        outRow.podData =
            store->podBytes.data() + rowIndex * store->podStride;
        outRow.podSize = store->schema.podSize;
        outRow.podAlignment = store->schema.podAlignment;
        return true;
    }

    bool WorldMassStore::TryGetTransformFromStore(
        const Store& store,
        size_t rowIndex,
        RuntimeObjectHandle object,
        GameProjectTransform& outTransform) noexcept {
        if (rowIndex >= store.objects.size() ||
            store.objects[rowIndex] != object) {
            return false;
        }
        outTransform.position = store.positions[rowIndex];
        outTransform.rotation = store.rotations[rowIndex];
        outTransform.scale = store.scales[rowIndex];
        return true;
    }

    bool WorldMassStore::TryGetTransform(
        ComponentTypeId typeId,
        size_t rowIndex,
        RuntimeObjectHandle object,
        GameProjectTransform& outTransform) const noexcept {
        const Store* store = FindStore(typeId);
        return store != nullptr && TryGetTransformFromStore(
            *store,
            rowIndex,
            object,
            outTransform);
    }

    bool WorldMassStore::SetTransformInStore(
        Store& store,
        size_t rowIndex,
        RuntimeObjectHandle object,
        const GameProjectTransform& transform) noexcept {
        if (rowIndex >= store.objects.size() ||
            store.objects[rowIndex] != object) {
            return false;
        }
        store.positions[rowIndex] = transform.position;
        store.rotations[rowIndex] = transform.rotation;
        store.scales[rowIndex] = transform.scale;
        return true;
    }

    bool WorldMassStore::SetTransform(
        ComponentTypeId typeId,
        size_t rowIndex,
        RuntimeObjectHandle object,
        const GameProjectTransform& transform) noexcept {
        Store* store = FindStore(typeId);
        return store != nullptr && SetTransformInStore(
            *store,
            rowIndex,
            object,
            transform);
    }

    size_t WorldMassStore::GetCount(ComponentTypeId typeId) const noexcept {
        const Store* store = FindStore(typeId);
        return store != nullptr ? store->objects.size() : 0u;
    }

    size_t WorldMassStore::GetTotalCount() const noexcept {
        return totalCount_;
    }

    void WorldMassStore::Clear() noexcept {
        stores_.clear();
        totalCount_ = 0u;
    }

} // namespace HIKARI
