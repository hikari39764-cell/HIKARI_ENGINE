#include "Scene/HIKARI_World.h"

#include <algorithm>
#include <exception>
#include <limits>

#include "Core/HIKARI_Logger.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Internal/HIKARI_WorldCapacity.h"

namespace HIKARI {

    bool World::PrepareComponentAttachment(
        IComponent& component,
        ComponentTypeId declaredTypeId) {
        if (!CanEnterStructureCallback() ||
            component.worldComponentTypeId_ != 0u ||
            component.worldComponentIndex_ !=
                kInvalidWorldComponentIndex) {
            return false;
        }

        const std::string_view typeName = component.GetTypeName();
        const ComponentTypeId computedTypeId =
            MakeComponentTypeId(typeName);
        if (!computedTypeId.IsValid() ||
            (declaredTypeId.IsValid() &&
             declaredTypeId != computedTypeId)) {
            HIKARI_LOG_ERROR(
                "[World] component declared type id does not match "
                "its canonical type name.");
            return false;
        }

        auto found = componentBuckets_.find(computedTypeId.value);
        if (found == componentBuckets_.end()) {
            WorldComponentBucket bucket{};
            bucket.typeId = computedTypeId;
            bucket.typeName = std::string(typeName);
            bucket.components.reserve(1u);
            found = componentBuckets_.emplace(
                computedTypeId.value,
                std::move(bucket)).first;
        }
        else if (found->second.typeName != typeName) {
            HIKARI_LOG_ERROR(
                "[World] component type id collision rejected: " +
                found->second.typeName + " <-> " +
                std::string(typeName));
            return false;
        }

        auto& components = found->second.components;
        if (components.size() >=
            static_cast<size_t>(
                (std::numeric_limits<uint32_t>::max)()) ||
            found->second.nextAttachSequence == 0u) {
            return false;
        }
        const size_t nextCount = components.size() + 1u;
        components.reserve(INTERNAL::GrowWorldCapacity(
            components.capacity(),
            nextCount,
            (std::min)(
                components.max_size(),
                static_cast<size_t>(
                    (std::numeric_limits<uint32_t>::max)()))));
        component.worldComponentTypeId_ = computedTypeId.value;
        return true;
    }

    void World::CancelComponentAttachment(
        IComponent& component) noexcept {
        component.worldComponentTypeId_ = 0u;
        component.worldComponentIndex_ = kInvalidWorldComponentIndex;
    }

    bool World::CommitComponentAttachment(
        GameObject& object,
        IComponent& component) noexcept {
        if (component.worldComponentTypeId_ == 0u ||
            component.worldComponentIndex_ !=
                kInvalidWorldComponentIndex) {
            return false;
        }
        const auto found = componentBuckets_.find(
            component.worldComponentTypeId_);
        if (found == componentBuckets_.end()) {
            return false;
        }

        WorldComponentBucket& bucket = found->second;
        auto& components = bucket.components;
        if (components.size() >= components.capacity() ||
            components.size() >=
                static_cast<size_t>(
                    (std::numeric_limits<uint32_t>::max)()) ||
            bucket.nextAttachSequence == 0u) {
            return false;
        }

        const uint32_t index =
            static_cast<uint32_t>(components.size());
        const uint32_t previous = bucket.tail;
        try {
            components.push_back(IndexedWorldComponent{
                object.GetRuntimeHandle(),
                &component,
                previous,
                kInvalidWorldComponentIndex,
                bucket.nextAttachSequence
            });
        }
        catch (...) {
            return false;
        }

        ++bucket.nextAttachSequence;
        if (previous != kInvalidWorldComponentIndex) {
            components[previous].next = index;
        }
        else {
            bucket.head = index;
        }
        bucket.tail = index;
        component.worldComponentIndex_ = index;
        return true;
    }

    void World::RemoveComponentFromIndex(
        IComponent& component) noexcept {
        if (component.worldComponentTypeId_ == 0u ||
            component.worldComponentIndex_ ==
                kInvalidWorldComponentIndex) {
            return;
        }
        const auto found = componentBuckets_.find(
            component.worldComponentTypeId_);
        if (found == componentBuckets_.end()) {
            HIKARI_LOG_ERROR(
                "[World] component index bucket invariant failed.");
            std::terminate();
        }

        WorldComponentBucket& bucket = found->second;
        auto& components = bucket.components;
        const uint32_t index = component.worldComponentIndex_;
        if (index >= components.size() ||
            components[index].component != &component) {
            HIKARI_LOG_ERROR(
                "[World] component dense-index invariant failed.");
            std::terminate();
        }

        const IndexedWorldComponent removed = components[index];
        if (removed.previous != kInvalidWorldComponentIndex) {
            components[removed.previous].next = removed.next;
        }
        else {
            bucket.head = removed.next;
        }
        if (removed.next != kInvalidWorldComponentIndex) {
            components[removed.next].previous = removed.previous;
        }
        else {
            bucket.tail = removed.previous;
        }

        const uint32_t lastIndex =
            static_cast<uint32_t>(components.size() - 1u);
        if (index != lastIndex) {
            components[index] = components[lastIndex];
            IndexedWorldComponent& moved = components[index];
            if (moved.previous != kInvalidWorldComponentIndex) {
                components[moved.previous].next = index;
            }
            else {
                bucket.head = index;
            }
            if (moved.next != kInvalidWorldComponentIndex) {
                components[moved.next].previous = index;
            }
            else {
                bucket.tail = index;
            }
            moved.component->worldComponentIndex_ = index;
        }
        components.pop_back();
        CancelComponentAttachment(component);
    }

    bool World::VisitIndexedComponents(
        ComponentTypeId typeId,
        void* context,
        IndexedComponentVisitorFn visitor) noexcept {
        if (!IsOwnerThread() ||
            !typeId.IsValid() ||
            visitor == nullptr) {
            return false;
        }
        const auto found = componentBuckets_.find(typeId.value);
        if (found == componentBuckets_.end()) {
            return true;
        }

        const StructureReadScope structureRead =
            AcquireStructureReadScope();
        const WorldComponentBucket& bucket = found->second;
        uint32_t index = bucket.head;
        uint64_t previousSequence = 0u;
        size_t visitedCount = 0u;
        while (index != kInvalidWorldComponentIndex) {
            if (index >= bucket.components.size() ||
                visitedCount >= bucket.components.size()) {
                return false;
            }
            const IndexedWorldComponent& entry =
                bucket.components[index];
            if (entry.component == nullptr ||
                entry.attachSequence <= previousSequence) {
                return false;
            }
            previousSequence = entry.attachSequence;
            const uint32_t next = entry.next;
            if (!visitor(
                    context,
                    entry.object,
                    entry.component)) {
                return false;
            }
            ++visitedCount;
            index = next;
        }
        return visitedCount == bucket.components.size();
    }

} // namespace HIKARI
