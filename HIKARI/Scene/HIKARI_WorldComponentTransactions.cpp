#include "Scene/HIKARI_World.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <typeindex>

#include "Core/HIKARI_Logger.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/Components/HIKARI_IComponent.h"
#include "Scene/Internal/HIKARI_WorldCapacity.h"

namespace HIKARI {

    bool World::PrepareComponentAttachment(
        GameObject& object,
        IComponent& component,
        ComponentTypeId declaredTypeId) {
        if (!CanEnterStructureCallback(&object) ||
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
        const ComponentTypeInfo* typeInfo = componentRegistry_ != nullptr
            ? componentRegistry_->Find(bucket.typeId)
            : nullptr;
        const bool affectsRenderState =
            typeInfo != nullptr && typeInfo->affectsRenderState;
        const bool firstRenderContributor = affectsRenderState &&
            object.renderStateContributorCount_ == 0u;
        if (components.size() >= components.capacity() ||
            components.size() >=
                static_cast<size_t>(
                    (std::numeric_limits<uint32_t>::max)()) ||
            bucket.nextAttachSequence == 0u ||
            (affectsRenderState &&
             object.renderStateContributorCount_ ==
                (std::numeric_limits<uint32_t>::max)())) {
            return false;
        }
        if (firstRenderContributor) {
            if (renderTrackedObjectCount_ >=
                    renderDirtyObjects_.max_size()) {
                return false;
            }
            try {
                renderDirtyObjects_.reserve(
                    INTERNAL::GrowWorldCapacity(
                        renderDirtyObjects_.capacity(),
                        renderTrackedObjectCount_ + 1u,
                        renderDirtyObjects_.max_size()));
            }
            catch (...) {
                return false;
            }
        }

        const uint32_t index =
            static_cast<uint32_t>(components.size());
        const uint32_t previous = bucket.tail;
        const uint64_t objectKey = object.GetRuntimeHandle().ToValue();
        const std::type_info& nativeType = typeid(component);
        if (objectKey == 0u ||
            (bucket.nativeType != nullptr &&
             *bucket.nativeType != nativeType)) {
            return false;
        }

        if (!AddNativeComponentTypeBinding(component, bucket.typeId)) {
            return false;
        }
        bool firstComponentInserted = false;
        try {
            firstComponentInserted = bucket.firstByObject.try_emplace(
                objectKey,
                index).second;
            components.push_back(IndexedWorldComponent{
                object.GetRuntimeHandle(),
                &component,
                previous,
                kInvalidWorldComponentIndex,
                firstComponentInserted
                    ? bucket.objectTail
                    : kInvalidWorldComponentIndex,
                kInvalidWorldComponentIndex,
                bucket.nextAttachSequence,
                firstComponentInserted ? bucket.nextAttachSequence : 0u,
                affectsRenderState
            });
        }
        catch (...) {
            if (firstComponentInserted) {
                bucket.firstByObject.erase(objectKey);
            }
            RemoveNativeComponentTypeBinding(component, bucket.typeId);
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
        if (bucket.nativeType == nullptr) {
            bucket.nativeType = &nativeType;
        }
        if (firstComponentInserted) {
            const uint32_t previousObject =
                components[index].previousObject;
            if (previousObject != kInvalidWorldComponentIndex) {
                components[previousObject].nextObject = index;
            }
            else {
                bucket.objectHead = index;
            }
            bucket.objectTail = index;
        }
        component.worldComponentIndex_ = index;
        if (affectsRenderState) {
            ++object.renderStateContributorCount_;
            if (firstRenderContributor) {
                ++renderTrackedObjectCount_;
            }
        }
        return true;
    }

    bool World::AddNativeComponentTypeBinding(
        IComponent& component,
        ComponentTypeId typeId) noexcept {
        const std::type_index nativeType(typeid(component));
        bool bindingInserted = false;
        try {
            const auto inserted = nativeComponentTypeIds_.try_emplace(
                nativeType);
            bindingInserted = inserted.second;
            NativeComponentTypeBinding& binding = inserted.first->second;
            auto typeCount = std::find_if(
                binding.typeCounts.begin(),
                binding.typeCounts.end(),
                [typeId](const NativeComponentTypeCount& candidate) {
                    return candidate.typeId == typeId;
                });
            if (typeCount == binding.typeCounts.end()) {
                binding.typeCounts.push_back(
                    NativeComponentTypeCount{ typeId, 1u });
            }
            else {
                if (typeCount->liveCount ==
                    (std::numeric_limits<size_t>::max)()) {
                    return false;
                }
                ++typeCount->liveCount;
            }
            binding.resolvedTypeId = binding.typeCounts.size() == 1u
                ? binding.typeCounts.front().typeId
                : ComponentTypeId{};
            return true;
        }
        catch (...) {
            if (bindingInserted) {
                nativeComponentTypeIds_.erase(nativeType);
            }
            return false;
        }
    }

    void World::RemoveNativeComponentTypeBinding(
        IComponent& component,
        ComponentTypeId typeId) noexcept {
        const std::type_index nativeType(typeid(component));
        const auto bindingFound = nativeComponentTypeIds_.find(nativeType);
        if (bindingFound == nativeComponentTypeIds_.end()) {
            HIKARI_LOG_ERROR(
                "[World] native component type binding is missing.");
            std::terminate();
        }
        NativeComponentTypeBinding& binding = bindingFound->second;
        const auto typeCount = std::find_if(
            binding.typeCounts.begin(),
            binding.typeCounts.end(),
            [typeId](const NativeComponentTypeCount& candidate) {
                return candidate.typeId == typeId;
            });
        if (typeCount == binding.typeCounts.end() ||
            typeCount->liveCount == 0u) {
            HIKARI_LOG_ERROR(
                "[World] native component type count is invalid.");
            std::terminate();
        }
        --typeCount->liveCount;
        if (typeCount->liveCount == 0u) {
            binding.typeCounts.erase(typeCount);
        }
        if (binding.typeCounts.empty()) {
            nativeComponentTypeIds_.erase(bindingFound);
            return;
        }
        binding.resolvedTypeId = binding.typeCounts.size() == 1u
            ? binding.typeCounts.front().typeId
            : ComponentTypeId{};
    }

    World::WorldCommandCheckpoint
        World::BeginComponentCallback(GameObject& object) {
        if (componentCallbackActive_ ||
            !CanEnterStructureCallback(&object)) {
            throw std::logic_error(
                "World component callback transaction rejected.");
        }
        const WorldCommandCheckpoint checkpoint{
            pendingCommands_.size(),
            reservationStateJournal_.size()
        };
        const size_t desiredJournalCapacity = (std::min)(
            kWorldCommandMaximumCapacity,
            pendingCommands_.size() +
                kWorldSpawnBatchMaximumCapacity);
        reservationStateJournal_.reserve(
            desiredJournalCapacity);
        componentCallbackActive_ = true;
        return checkpoint;
    }

    void World::CommitComponentCallback(
        WorldCommandCheckpoint checkpoint) noexcept {
        if (!componentCallbackActive_) {
            return;
        }
        if (spawnMaterializationObject_ == nullptr &&
            checkpoint.reservationJournalCount <=
            reservationStateJournal_.size()) {
            reservationStateJournal_.resize(
                checkpoint.reservationJournalCount);
        }
        componentCallbackActive_ = false;
    }

    void World::RollbackComponentCallback(
        WorldCommandCheckpoint checkpoint) noexcept {
        if (!componentCallbackActive_) {
            return;
        }

        RollbackCommandCheckpoint(checkpoint);
        componentCallbackActive_ = false;
    }

    void World::RollbackCommandCheckpoint(
        WorldCommandCheckpoint checkpoint) noexcept {

        for (size_t index = reservationStateJournal_.size();
            index-- > checkpoint.reservationJournalCount;) {
            const ReservedStateJournalEntry& journal =
                reservationStateJournal_[index];
            if (!journal.object.IsValid() ||
                journal.object.slot >= objectSlots_.size()) {
                continue;
            }
            ObjectSlot& slot = objectSlots_[journal.object.slot];
            if (slot.generation == journal.object.generation &&
                slot.state == ObjectSlotState::ReservedCanceled &&
                slot.object == nullptr) {
                slot.state = journal.previousState;
            }
        }
        reservationStateJournal_.resize(
            checkpoint.reservationJournalCount);

        if (checkpoint.pendingCommandCount <=
            pendingCommands_.size()) {
            for (size_t index = checkpoint.pendingCommandCount;
                index < pendingCommands_.size();
                ++index) {
                const PendingWorldCommand& command =
                    pendingCommands_[index];
                RecordCommandFailure(
                    command,
                    WorldMutationResult::CommandCanceled,
                    WorldCommandPhase::Unknown);
                ReleaseSpawnCommandReservations(command);
            }
            pendingCommands_.resize(
                checkpoint.pendingCommandCount);
        }
    }

} // namespace HIKARI
