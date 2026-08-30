#include "Scene/HIKARI_World.h"

#include <exception>
#include <limits>

#include "Core/HIKARI_Logger.h"
#include "Scene/Components/HIKARI_IComponent.h"

namespace HIKARI {

    IComponent* World::FindFirstIndexedComponent(
        ComponentTypeId typeId,
        RuntimeObjectHandle object) noexcept {
        if (!IsOwnerThread() || worldClosing_ ||
            !typeId.IsValid() || !object.IsValid()) {
            return nullptr;
        }
        const auto bucket = componentBuckets_.find(typeId.value);
        if (bucket == componentBuckets_.end()) {
            return nullptr;
        }
        const auto member = bucket->second.firstByObject.find(
            object.ToValue());
        if (member == bucket->second.firstByObject.end() ||
            member->second >= bucket->second.components.size()) {
            return nullptr;
        }
        IndexedWorldComponent& entry =
            bucket->second.components[member->second];
        if (entry.component == nullptr ||
            entry.component->worldComponentIndex_ != member->second ||
            entry.component->worldComponentTypeId_ != typeId.value) {
            return nullptr;
        }
        GameObject* owner = FindObject(object);
        return owner != nullptr && !owner->destroying_ &&
            entry.object == object
            ? entry.component
            : nullptr;
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
        const uint64_t objectKey = removed.object.ToValue();
        auto firstForObject = bucket.firstByObject.find(objectKey);
        if (objectKey == 0u ||
            firstForObject == bucket.firstByObject.end()) {
            HIKARI_LOG_ERROR(
                "[World] component membership invariant failed.");
            std::terminate();
        }
        if (firstForObject->second == index) {
            uint32_t replacementIndex = kInvalidWorldComponentIndex;
            GameObject* owner = component.GetOwner();
            if (owner != nullptr) {
                for (const auto& candidate : owner->components_) {
                    if (candidate.get() != &component &&
                        candidate->worldComponentTypeId_ ==
                            component.worldComponentTypeId_) {
                        const uint32_t candidateIndex =
                            candidate->worldComponentIndex_;
                        if (candidateIndex >= components.size() ||
                            components[candidateIndex].component !=
                                candidate.get() ||
                            components[candidateIndex].object !=
                                removed.object) {
                            HIKARI_LOG_ERROR(
                                "[World] replacement component invariant failed.");
                            std::terminate();
                        }
                        replacementIndex = candidateIndex;
                        break;
                    }
                }
            }
            if (replacementIndex != kInvalidWorldComponentIndex) {
                IndexedWorldComponent& replacement =
                    components[replacementIndex];
                if (replacement.previousObject !=
                        kInvalidWorldComponentIndex ||
                    replacement.nextObject !=
                        kInvalidWorldComponentIndex) {
                    HIKARI_LOG_ERROR(
                        "[World] replacement object-link invariant failed.");
                    std::terminate();
                }
                replacement.previousObject = removed.previousObject;
                replacement.nextObject = removed.nextObject;
                replacement.objectAttachSequence =
                    removed.objectAttachSequence;
                if (removed.previousObject !=
                        kInvalidWorldComponentIndex) {
                    components[removed.previousObject].nextObject =
                        replacementIndex;
                }
                else {
                    bucket.objectHead = replacementIndex;
                }
                if (removed.nextObject !=
                        kInvalidWorldComponentIndex) {
                    components[removed.nextObject].previousObject =
                        replacementIndex;
                }
                else {
                    bucket.objectTail = replacementIndex;
                }
                firstForObject->second = replacementIndex;
            }
            else {
                if (removed.previousObject !=
                        kInvalidWorldComponentIndex) {
                    components[removed.previousObject].nextObject =
                        removed.nextObject;
                }
                else {
                    bucket.objectHead = removed.nextObject;
                }
                if (removed.nextObject !=
                        kInvalidWorldComponentIndex) {
                    components[removed.nextObject].previousObject =
                        removed.previousObject;
                }
                else {
                    bucket.objectTail = removed.previousObject;
                }
                bucket.firstByObject.erase(firstForObject);
            }
        }
        else if (removed.previousObject != kInvalidWorldComponentIndex ||
            removed.nextObject != kInvalidWorldComponentIndex) {
            HIKARI_LOG_ERROR(
                "[World] duplicate component owns object links.");
            std::terminate();
        }
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
            const uint64_t movedObjectKey = moved.object.ToValue();
            auto movedFirst = bucket.firstByObject.find(movedObjectKey);
            if (movedFirst == bucket.firstByObject.end()) {
                HIKARI_LOG_ERROR(
                    "[World] moved component membership is missing.");
                std::terminate();
            }
            if (movedFirst->second == lastIndex) {
                movedFirst->second = index;
                if (moved.previousObject !=
                        kInvalidWorldComponentIndex) {
                    components[moved.previousObject].nextObject = index;
                }
                else {
                    bucket.objectHead = index;
                }
                if (moved.nextObject !=
                        kInvalidWorldComponentIndex) {
                    components[moved.nextObject].previousObject = index;
                }
                else {
                    bucket.objectTail = index;
                }
            }
            else if (moved.previousObject !=
                    kInvalidWorldComponentIndex ||
                moved.nextObject != kInvalidWorldComponentIndex) {
                HIKARI_LOG_ERROR(
                    "[World] moved duplicate owns object links.");
                std::terminate();
            }
            moved.component->worldComponentIndex_ = index;
        }
        components.pop_back();
        if (components.empty()) {
            if (!bucket.firstByObject.empty() ||
                bucket.head != kInvalidWorldComponentIndex ||
                bucket.tail != kInvalidWorldComponentIndex ||
                bucket.objectHead != kInvalidWorldComponentIndex ||
                bucket.objectTail != kInvalidWorldComponentIndex) {
                HIKARI_LOG_ERROR(
                    "[World] empty component bucket retains membership.");
                std::terminate();
            }
            bucket.nativeType = nullptr;
        }
        RemoveNativeComponentTypeBinding(
            component,
            ComponentTypeId{ component.worldComponentTypeId_ });
        CancelComponentAttachment(component);
    }

    bool World::VisitIndexedComponents(
        const WorldComponentQueryPlan& plan,
        void* context,
        IndexedComponentVisitorFn visitor) noexcept {
        const std::span<const ComponentTypeId> typeIds =
            plan.requiredTypes;
        if (!IsOwnerThread() ||
            worldClosing_ ||
            typeIds.empty() ||
            typeIds.data() == nullptr ||
            typeIds.size() > kMaximumWorldComponentQueryTypeCount ||
            plan.match != WorldComponentQueryMatch::All ||
            plan.cardinality >= WorldComponentQueryCardinality::Count ||
            plan.anchor >= WorldComponentQueryAnchor::Count ||
            plan.order != WorldComponentQueryOrder::AnchorAttachment ||
            (typeIds.size() > 1u &&
             plan.cardinality !=
                WorldComponentQueryCardinality::FirstPerObject) ||
            visitor == nullptr) {
            return false;
        }

        std::array<const WorldComponentBucket*,
            kMaximumWorldComponentQueryTypeCount> buckets{};
        size_t anchorTerm = 0u;
        size_t anchorCardinality = (std::numeric_limits<size_t>::max)();
        for (size_t term = 0u; term < typeIds.size(); ++term) {
            if (!typeIds[term].IsValid()) {
                return false;
            }
            for (size_t previousTerm = 0u;
                previousTerm < term;
                ++previousTerm) {
                if (typeIds[previousTerm] == typeIds[term]) {
                    return false;
                }
            }
        }

        bool emptyResult = false;
        for (size_t term = 0u; term < typeIds.size(); ++term) {
            const auto found = componentBuckets_.find(typeIds[term].value);
            if (found == componentBuckets_.end() ||
                found->second.components.empty()) {
                emptyResult = true;
                continue;
            }
            buckets[term] = &found->second;
            const size_t cardinality = plan.cardinality ==
                    WorldComponentQueryCardinality::AllEntries
                ? found->second.components.size()
                : found->second.firstByObject.size();
            if (plan.anchor ==
                    WorldComponentQueryAnchor::SmallestUniqueBucket &&
                cardinality < anchorCardinality) {
                anchorTerm = term;
                anchorCardinality = cardinality;
            }
        }
        if (emptyResult) {
            return true;
        }

        const StructureReadScope structureRead =
            AcquireStructureReadScope();
        const WorldComponentBucket& anchor = *buckets[anchorTerm];
        const bool firstPerObject = plan.cardinality ==
            WorldComponentQueryCardinality::FirstPerObject;
        uint32_t index = firstPerObject
            ? anchor.objectHead
            : anchor.head;
        uint64_t previousSequence = 0u;
        uint32_t previousIndex = kInvalidWorldComponentIndex;
        size_t visitedCount = 0u;
        const size_t expectedVisitCount = firstPerObject
            ? anchor.firstByObject.size()
            : anchor.components.size();
        while (index != kInvalidWorldComponentIndex) {
            if (index >= anchor.components.size() ||
                visitedCount >= expectedVisitCount) {
                return false;
            }
            const IndexedWorldComponent& entry =
                anchor.components[index];
            const uint64_t sequence = firstPerObject
                ? entry.objectAttachSequence
                : entry.attachSequence;
            const uint32_t linkedPrevious = firstPerObject
                ? entry.previousObject
                : entry.previous;
            if (entry.component == nullptr ||
                entry.component->worldComponentIndex_ != index ||
                entry.component->worldComponentTypeId_ !=
                    typeIds[anchorTerm].value ||
                linkedPrevious != previousIndex ||
                sequence == 0u ||
                sequence <= previousSequence) {
                return false;
            }
            previousSequence = sequence;
            const uint32_t next = firstPerObject
                ? entry.nextObject
                : entry.next;
            if (firstPerObject) {
                const auto first = anchor.firstByObject.find(
                    entry.object.ToValue());
                if (first == anchor.firstByObject.end() ||
                    first->second != index) {
                    return false;
                }
            }
            GameObject* object = FindObject(entry.object);
            if (object == nullptr) {
                return false;
            }
            if (!object->destroying_) {
                std::array<IComponent*,
                    kMaximumWorldComponentQueryTypeCount> components{};
                bool matches = true;
                for (size_t term = 0u; term < typeIds.size(); ++term) {
                    if (term == anchorTerm) {
                        components[term] = entry.component;
                        continue;
                    }
                    const auto joined = buckets[term]->firstByObject.find(
                        entry.object.ToValue());
                    if (joined == buckets[term]->firstByObject.end() ||
                        joined->second >=
                            buckets[term]->components.size()) {
                        matches = false;
                        break;
                    }
                    const IndexedWorldComponent& joinedEntry =
                        buckets[term]->components[joined->second];
                    if (joinedEntry.object != entry.object ||
                        joinedEntry.component == nullptr ||
                        joinedEntry.component->worldComponentIndex_ !=
                            joined->second ||
                        joinedEntry.component->worldComponentTypeId_ !=
                            typeIds[term].value) {
                        return false;
                    }
                    components[term] = joinedEntry.component;
                }
                if (matches &&
                    !visitor(
                        context,
                        entry.object,
                        components.data(),
                        static_cast<uint32_t>(typeIds.size()))) {
                    return false;
                }
            }
            ++visitedCount;
            previousIndex = index;
            index = next;
        }
        return visitedCount == expectedVisitCount;
    }

} // namespace HIKARI
