#include "Scene/HIKARI_WorldMassStore.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "Scene/Internal/HIKARI_WorldCapacity.h"

namespace HIKARI {
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
        const GameProjectWorldMassRenderProxyDesc* renderProxy,
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
            !IsValidWorldMassSchema(schema) ||
            (renderProxy != nullptr &&
                (!IsValidWorldMassRenderProxy(*renderProxy) ||
                    renderProxy->typeId != schema.typeId ||
                    renderProxy->layoutHash != schema.layoutHash ||
                    renderProxy->schemaVersion != schema.schemaVersion))) {
            return false;
        }
        for (size_t index = 0u; index < sourceIndices.size(); ++index) {
            const uint32_t sourceIndex = sourceIndices[index];
            if (!objects[index].IsValid() ||
                sourceIndex >= transforms.size() ||
                !INTERNAL::IsValidWorldMassTransform(
                    transforms[sourceIndex].position,
                    transforms[sourceIndex].rotation,
                    transforms[sourceIndex].scale) ||
                !INTERNAL::IsFiniteWorldMassVector(
                    velocities[sourceIndex])) {
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
            if (renderProxy != nullptr) {
                store.renderProxy = *renderProxy;
                store.hasRenderProxy = true;
                try {
                    renderStoreTypeOrder_.push_back(schema.typeId.value);
                    std::sort(
                        renderStoreTypeOrder_.begin(),
                        renderStoreTypeOrder_.end());
                }
                catch (...) {
                    const auto ordered = std::lower_bound(
                        renderStoreTypeOrder_.begin(),
                        renderStoreTypeOrder_.end(),
                        schema.typeId.value);
                    if (ordered != renderStoreTypeOrder_.end() &&
                        *ordered == schema.typeId.value) {
                        renderStoreTypeOrder_.erase(ordered);
                    }
                    stores_.erase(schema.typeId.value);
                    throw;
                }
            }
        }
        else if (!IsSameWorldMassSchema(store.schema, schema) ||
            store.hasRenderProxy != (renderProxy != nullptr) ||
            (renderProxy != nullptr &&
                !IsSameWorldMassRenderProxy(
                    store.renderProxy,
                    *renderProxy))) {
            return false;
        }
        const auto rollbackInsertedStore = [&]() noexcept {
            if (!inserted) {
                return;
            }
            if (store.hasRenderProxy) {
                const auto ordered = std::lower_bound(
                    renderStoreTypeOrder_.begin(),
                    renderStoreTypeOrder_.end(),
                    schema.typeId.value);
                if (ordered != renderStoreTypeOrder_.end() &&
                    *ordered == schema.typeId.value) {
                    renderStoreTypeOrder_.erase(ordered);
                }
            }
            stores_.erase(schema.typeId.value);
        };
        if (objects.size() > store.objects.max_size() -
            store.objects.size()) {
            rollbackInsertedStore();
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
                store.velocities.max_size(),
                store.hasRenderProxy
                    ? store.renderInstances.max_size()
                    : store.objects.max_size()
            }));
        if (capacity >
            store.podBytes.max_size() / store.podStride) {
            rollbackInsertedStore();
            return false;
        }
        const size_t previousCount = store.objects.size();
        const size_t firstByteOffset =
            previousCount * store.podStride;
        try {
            store.objects.reserve(capacity);
            store.positions.reserve(capacity);
            store.rotations.reserve(capacity);
            store.scales.reserve(capacity);
            store.velocities.reserve(capacity);
            store.podBytes.reserve(capacity * store.podStride);
            if (store.hasRenderProxy) {
                store.renderInstances.reserve(capacity);
                store.renderDirtyIndices.reserve(capacity);
                store.renderDirtyListPositions.reserve(capacity);
            }
            store.objects.resize(nextCount);
            store.positions.resize(nextCount);
            store.rotations.resize(nextCount);
            store.scales.resize(nextCount);
            store.velocities.resize(nextCount);
            store.podBytes.resize(nextCount * store.podStride);
            if (store.hasRenderProxy) {
                store.renderInstances.resize(nextCount);
                store.renderDirtyListPositions.resize(
                    nextCount,
                    UINT32_MAX);
            }
        }
        catch (...) {
            store.objects.resize(previousCount);
            store.positions.resize(previousCount);
            store.rotations.resize(previousCount);
            store.scales.resize(previousCount);
            store.velocities.resize(previousCount);
            store.podBytes.resize(previousCount * store.podStride);
            store.renderInstances.resize(
                store.hasRenderProxy ? previousCount : 0u);
            store.renderDirtyListPositions.resize(
                store.hasRenderProxy ? previousCount : 0u);
            rollbackInsertedStore();
            return false;
        }
        for (size_t index = 0u; index < objects.size(); ++index) {
            const size_t sourceIndex = sourceIndices[index];
            const size_t targetIndex = previousCount + index;
            store.objects[targetIndex] = objects[index];
            store.positions[targetIndex] =
                transforms[sourceIndex].position;
            store.rotations[targetIndex] =
                transforms[sourceIndex].rotation;
            store.scales[targetIndex] = transforms[sourceIndex].scale;
            store.velocities[targetIndex] = velocities[sourceIndex];
            const size_t targetByteOffset =
                firstByteOffset + index * store.podStride;
            std::memcpy(
                store.podBytes.data() + targetByteOffset,
                podData + sourceIndex * podStride,
                schema.podSize);
            if (store.hasRenderProxy) {
                store.renderInstances[targetIndex] = BuildRenderInstance(
                    objects[index],
                    store.positions[targetIndex],
                    store.rotations[targetIndex],
                    store.scales[targetIndex]);
                MarkRenderDirty(store, targetIndex);
            }
        }
        if (store.hasRenderProxy) {
            AdvanceRenderLayoutVersion(store);
        }
        outFirstRowIndex = previousCount;
        totalCount_ += objects.size();
        AdvanceMutationVersion(store);
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
        if (store->hasRenderProxy) {
            if (!store->renderDirtyAll) {
                for (size_t row = store->renderInstances.size();
                    row > firstRowIndex;
                    --row) {
                    RemoveRenderDirty(*store, row - 1u);
                }
            }
            store->renderInstances.resize(firstRowIndex);
            store->renderDirtyListPositions.resize(firstRowIndex);
            store->renderDirtyRanges.clear();
            AdvanceRenderLayoutVersion(*store);
        }
        totalCount_ -= objects.size();
        if (!objects.empty()) {
            AdvanceMutationVersion(*store);
        }
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
        if (store.hasRenderProxy && !store.renderDirtyAll) {
            RemoveRenderDirty(store, rowIndex);
        }
        if (rowIndex != lastIndex) {
            outMovedObject = store.objects[lastIndex];
            store.objects[rowIndex] = outMovedObject;
            store.positions[rowIndex] = store.positions[lastIndex];
            store.rotations[rowIndex] = store.rotations[lastIndex];
            store.scales[rowIndex] = store.scales[lastIndex];
            store.velocities[rowIndex] = store.velocities[lastIndex];
            if (store.hasRenderProxy) {
                store.renderInstances[rowIndex] =
                    store.renderInstances[lastIndex];
                if (!store.renderDirtyAll) {
                    const uint32_t movedDirtyPosition =
                        store.renderDirtyListPositions[lastIndex];
                    if (movedDirtyPosition != UINT32_MAX &&
                        movedDirtyPosition <
                            store.renderDirtyIndices.size()) {
                        store.renderDirtyIndices[movedDirtyPosition] =
                            static_cast<uint32_t>(rowIndex);
                        store.renderDirtyListPositions[rowIndex] =
                            movedDirtyPosition;
                        store.renderDirtyListPositions[lastIndex] =
                            UINT32_MAX;
                        store.renderDirtyIndicesSorted = false;
                    }
                }
            }
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
        if (store.hasRenderProxy) {
            store.renderInstances.pop_back();
            store.renderDirtyListPositions.pop_back();
            if (rowIndex != lastIndex) {
                MarkRenderDirty(store, rowIndex);
            }
            store.renderDirtyRanges.clear();
            AdvanceRenderLayoutVersion(store);
        }
        --totalCount_;
        AdvanceMutationVersion(store);
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

    bool WorldMassStore::TryGetObject(
        ComponentTypeId typeId,
        size_t rowIndex,
        RuntimeObjectHandle& outObject) const noexcept {
        outObject = {};
        const Store* store = FindStore(typeId);
        if (store == nullptr || rowIndex >= store->objects.size()) {
            return false;
        }
        outObject = store->objects[rowIndex];
        return outObject.IsValid();
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
        const bool renderChanged = store.hasRenderProxy &&
            HasChangedTransform(
                transform.position,
                transform.rotation,
                transform.scale,
                store.renderInstances[rowIndex]);
        store.positions[rowIndex] = transform.position;
        store.rotations[rowIndex] = transform.rotation;
        store.scales[rowIndex] = transform.scale;
        if (renderChanged) {
            store.renderInstances[rowIndex] = BuildRenderInstance(
                object,
                transform.position,
                transform.rotation,
                transform.scale);
            MarkRenderDirty(store, rowIndex);
            AdvanceRenderSourceVersion(store);
        }
        return true;
    }

    bool WorldMassStore::SetTransform(
        ComponentTypeId typeId,
        size_t rowIndex,
        RuntimeObjectHandle object,
        const GameProjectTransform& transform) noexcept {
        Store* store = FindStore(typeId);
        if (store == nullptr ||
            !INTERNAL::IsValidWorldMassTransform(
                transform.position,
                transform.rotation,
                transform.scale) ||
            rowIndex >= store->objects.size() ||
            store->objects[rowIndex] != object) {
            return false;
        }
        const bool changed =
            std::memcmp(
                &store->positions[rowIndex],
                &transform.position,
                sizeof(transform.position)) != 0 ||
            std::memcmp(
                &store->rotations[rowIndex],
                &transform.rotation,
                sizeof(transform.rotation)) != 0 ||
            std::memcmp(
                &store->scales[rowIndex],
                &transform.scale,
                sizeof(transform.scale)) != 0;
        if (!changed) {
            return true;
        }
        if (!SetTransformInStore(
                *store,
                rowIndex,
                object,
                transform)) {
            return false;
        }
        AdvanceMutationVersion(*store);
        return true;
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
        renderStoreTypeOrder_.clear();
        renderPublicationViews_.clear();
        totalCount_ = 0u;
        activeRenderPublication_ = {};
        renderPublicationInProgress_ = false;
    }

} // namespace HIKARI
