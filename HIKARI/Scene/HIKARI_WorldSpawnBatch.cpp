#include "Scene/HIKARI_World.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Core/HIKARI_Logger.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Internal/HIKARI_WorldCapacity.h"

namespace HIKARI {

    WorldMutationResult World::SubmitSpawnBatch(
        std::span<const WorldSpawnRequest> requests,
        std::span<WorldCommandSubmission> outSubmissions) {
        auto rejectAll = [&](WorldMutationResult result) noexcept {
            for (WorldCommandSubmission& submission :
                outSubmissions) {
                submission = WorldCommandSubmission{ result };
            }
            return result;
        };

        if (requests.size() != outSubmissions.size() ||
            requests.size() > kWorldSpawnBatchMaximumCapacity) {
            return rejectAll(
                WorldMutationResult::MutationDisallowed);
        }
        if (requests.empty()) {
            return WorldMutationResult::Success;
        }
        if (!IsOwnerThread()) {
            return rejectAll(WorldMutationResult::WrongThread);
        }
        if (worldClosing_) {
            return rejectAll(WorldMutationResult::WorldClosing);
        }
        if (requests.size() >
            kWorldCommandMaximumCapacity -
                pendingCommands_.size()) {
            return rejectAll(
                WorldMutationResult::CommandQueueFull);
        }

        for (const WorldSpawnRequest& request : requests) {
            if (request.name.empty() ||
                request.name.size() >= kWorldCommandObjectNameCapacity ||
                request.name.find('\0') != std::string_view::npos) {
                return rejectAll(WorldMutationResult::InvalidName);
            }
            // Batch v1 intentionally accepts only already-live parents.
            if (request.parent.IsValid() &&
                FindObject(request.parent) == nullptr) {
                return rejectAll(
                    WorldMutationResult::InvalidParentHandle);
            }
        }
        std::shared_ptr<WorldSpawnBatchPayload> spawnPayload{};
        const WorldMutationResult payloadAdmission =
            INTERNAL::BuildWorldSpawnBatchPayload(
                componentRegistry_,
                requests,
                spawnPayload);
        if (payloadAdmission != WorldMutationResult::Success) {
            return rejectAll(payloadAdmission);
        }

        const size_t previousCommandCount =
            pendingCommands_.size();
        std::array<RuntimeObjectHandle,
            kWorldSpawnBatchMaximumCapacity> reservedHandles{};
        size_t reservedCount = 0u;
        try {
            const size_t nextCommandCount =
                previousCommandCount + requests.size();
            pendingCommands_.reserve(
                INTERNAL::GrowWorldCapacity(
                    pendingCommands_.capacity(),
                    nextCommandCount,
                    kWorldCommandMaximumCapacity));

            const size_t newSlotCount =
                requests.size() > freeObjectSlots_.size()
                ? requests.size() - freeObjectSlots_.size()
                : 0u;
            if (newSlotCount >
                static_cast<size_t>(
                    (std::numeric_limits<uint32_t>::max)()) -
                    objectSlots_.size()) {
                return rejectAll(
                    WorldMutationResult::InternalError);
            }
            const size_t nextSlotCount =
                objectSlots_.size() + newSlotCount;
            const size_t slotCapacity =
                INTERNAL::GrowWorldCapacity(
                    (std::max)(
                        objectSlots_.capacity(),
                        freeObjectSlots_.capacity()),
                    nextSlotCount,
                    (std::min)(
                        static_cast<size_t>(
                            (std::numeric_limits<uint32_t>::max)()),
                        (std::min)(
                            objectSlots_.max_size(),
                            freeObjectSlots_.max_size())));
            objectSlots_.reserve(slotCapacity);
            freeObjectSlots_.reserve(slotCapacity);

            if (requests.size() >
                    objects_.max_size() - objects_.size() ||
                requests.size() >
                    renderDirtyObjects_.max_size() -
                        renderDirtyObjects_.size()) {
                return rejectAll(
                    WorldMutationResult::InternalError);
            }
            const size_t nextObjectCount =
                objects_.size() + requests.size();
            const size_t objectCapacity =
                INTERNAL::GrowWorldCapacity(
                    (std::max)(
                        objects_.capacity(),
                        renderDirtyObjects_.capacity()),
                    nextObjectCount,
                    (std::min)(
                        objects_.max_size(),
                        renderDirtyObjects_.max_size()));
            objects_.reserve(objectCapacity);
            renderDirtyObjects_.reserve(objectCapacity);

            for (; reservedCount < requests.size();
                ++reservedCount) {
                reservedHandles[reservedCount] =
                    ReserveObjectHandle();
                if (!reservedHandles[reservedCount].IsValid()) {
                    throw std::runtime_error(
                        "World batch handle reservation failed.");
                }
            }

            for (size_t index = 0u;
                index < requests.size();
                ++index) {
                const WorldSpawnRequest& request = requests[index];
                PendingWorldCommand command{};
                command.sequence = nextCommandSequence_++;
                if (command.sequence == 0u) {
                    command.sequence = nextCommandSequence_++;
                }
                command.token = AllocateCommandToken();
                command.kind = WorldCommandKind::Spawn;
                command.object = reservedHandles[index];
                command.parent = request.parent;
                command.transform = request.transform;
                command.spawnPayload = spawnPayload;
                command.spawnPayloadIndex =
                    static_cast<uint32_t>(index);
                std::memcpy(
                    command.name.data(),
                    request.name.data(),
                    request.name.size());
                command.name[request.name.size()] = '\0';
                pendingCommands_.push_back(command);
                objectSlots_[command.object.slot].reservationToken =
                    command.token;
                outSubmissions[index] = WorldCommandSubmission{
                    WorldMutationResult::Queued,
                    command.object,
                    command.token
                };
            }
        }
        catch (...) {
            pendingCommands_.resize(previousCommandCount);
            for (size_t index = 0u;
                index < reservedCount;
                ++index) {
                ReleaseReservedObjectHandle(
                    reservedHandles[index]);
            }
            return rejectAll(WorldMutationResult::InternalError);
        }
        return WorldMutationResult::Queued;
    }

    WorldMutationResult World::ApplyQueuedSpawn(
        const PendingWorldCommand& command) {
        if (!command.object.IsValid() ||
            command.object.slot >= objectSlots_.size()) {
            return WorldMutationResult::InvalidObjectHandle;
        }
        const uint32_t slotIndex = command.object.slot;
        if (objectSlots_[slotIndex].generation !=
            command.object.generation) {
            return WorldMutationResult::InvalidObjectHandle;
        }
        if (objectSlots_[slotIndex].state ==
            ObjectSlotState::ReservedCanceled) {
            ReleaseReservedObjectHandle(command.object);
            return WorldMutationResult::CommandCanceled;
        }
        if (objectSlots_[slotIndex].state != ObjectSlotState::Reserved) {
            return WorldMutationResult::InvalidObjectHandle;
        }
        const auto rejectReserved =
            [&](WorldMutationResult result) noexcept {
                if (IsReservedObjectHandle(command.object)) {
                    ReleaseReservedObjectHandle(command.object);
                }
                return result;
            };

        GameObject* parent = nullptr;
        if (command.parent.IsValid()) {
            parent = FindObject(command.parent);
            if (parent == nullptr) {
                return rejectReserved(
                    WorldMutationResult::InvalidParentHandle);
            }
        }

        const WorldSpawnObjectPayload* payload = nullptr;
        if (command.spawnPayload != nullptr) {
            if (command.spawnPayloadIndex >=
                command.spawnPayload->objects.size()) {
                return rejectReserved(
                    WorldMutationResult::InvalidComponentData);
            }
            payload = &command.spawnPayload->objects[
                command.spawnPayloadIndex];
        }
        struct PreparedComponent {
            std::unique_ptr<IComponent> component{};
            ComponentTypeId typeId{};
        };
        std::vector<PreparedComponent> prepared{};
        if (payload != nullptr && !payload->components.empty()) {
            if (componentRegistry_ == nullptr) {
                return rejectReserved(
                    WorldMutationResult::ComponentInitializationFailed);
            }
            try {
                prepared.reserve(payload->components.size());
                for (const WorldSpawnComponentPayload& source :
                    payload->components) {
                    const ComponentTypeInfo* info =
                        componentRegistry_->Find(source.typeId);
                    if (info == nullptr || !info->factory ||
                        info->registrationSource !=
                            ComponentRegistrationSource::GameProject) {
                        return rejectReserved(
                            WorldMutationResult::
                                ComponentInitializationFailed);
                    }
                    std::unique_ptr<IComponent> component = info->factory();
                    if (component == nullptr) {
                        return rejectReserved(
                            WorldMutationResult::
                                ComponentInitializationFailed);
                    }
                    if (!source.initialValuesJson.empty()) {
                        const nlohmann::json initialValues =
                            nlohmann::json::parse(
                                source.initialValuesJson.begin(),
                                source.initialValuesJson.end(),
                                nullptr,
                                false);
                        std::string diagnostic{};
                        if (initialValues.is_discarded() ||
                            !initialValues.is_object() ||
                            !component->TryDeserialize(
                                initialValues,
                                diagnostic)) {
                            HIKARI_LOG_ERROR(
                                "[World] spawn component initialization "
                                "failed: " + info->typeName +
                                (diagnostic.empty()
                                    ? std::string{}
                                    : " diagnostic=" + diagnostic));
                            return rejectReserved(
                                WorldMutationResult::
                                    ComponentInitializationFailed);
                        }
                    }
                    prepared.push_back(PreparedComponent{
                        std::move(component),
                        info->typeId
                    });
                }
            }
            catch (...) {
                return rejectReserved(WorldMutationResult::InternalError);
            }
        }

        if (objects_.size() >= objects_.max_size() ||
            objects_.size() >= renderDirtyObjects_.max_size()) {
            return rejectReserved(WorldMutationResult::InternalError);
        }
        const size_t nextObjectCount = objects_.size() + 1u;
        const size_t objectCapacity = INTERNAL::GrowWorldCapacity(
            (std::max)(
                objects_.capacity(),
                renderDirtyObjects_.capacity()),
            nextObjectCount,
            (std::min)(
                objects_.max_size(),
                renderDirtyObjects_.max_size()));
        objects_.reserve(objectCapacity);
        renderDirtyObjects_.reserve(objectCapacity);

        auto object = std::make_unique<GameObject>(command.name.data());
        GameObject* ptr = object.get();
        ptr->SetRuntimeHandle(command.object);
        bool objectStored = false;
        try {
            ptr->SetOwnerWorld(this);
            Transform3D localTransform{};
            localTransform.position = command.transform.position;
            localTransform.rotation = command.transform.rotation;
            localTransform.scale = command.transform.scale;
            (void)ptr->SetLocalTransform(localTransform);
            if (parent != nullptr &&
                !ptr->SetParentInternal(parent)) {
                throw std::runtime_error(
                    "Queued World spawn hierarchy rejected.");
            }
            const size_t storageIndex = objects_.size();
            objects_.push_back(std::move(object));
            objectStored = true;
            if (!CommitReservedObjectHandle(
                    command.object,
                    ptr,
                    storageIndex)) {
                throw std::runtime_error(
                    "Queued World spawn handle commit rejected.");
            }
        }
        catch (...) {
            (void)ptr->SetParentInternal(nullptr);
            RemoveRenderObjectDirty(ptr);
            ptr->SetOwnerWorld(nullptr);
            ptr->SetRuntimeHandle({});
            if (slotIndex < objectSlots_.size() &&
                objectSlots_[slotIndex].state == ObjectSlotState::Live) {
                ReleaseObjectHandle(command.object);
            }
            else {
                ReleaseReservedObjectHandle(command.object);
            }
            if (objectStored && !objects_.empty() &&
                objects_.back().get() == ptr) {
                objects_.pop_back();
            }
            throw;
        }
        if (prepared.empty()) {
            return WorldMutationResult::Success;
        }

        const WorldCommandCheckpoint spawnCheckpoint{
            pendingCommands_.size(),
            reservationStateJournal_.size()
        };
        bool materializationActive = false;
        WorldMutationResult failure =
            WorldMutationResult::ComponentInitializationFailed;
        try {
            if (spawnMaterializationObject_ != nullptr ||
                componentCallbackActive_ ||
                !commandFlushActive_) {
                throw std::logic_error(
                    "World spawn materialization transaction rejected.");
            }
            reservationStateJournal_.reserve(
                kWorldCommandMaximumCapacity);
            spawnMaterializationObject_ = ptr;
            materializationActive = true;
            bool attached = true;
            for (PreparedComponent& value : prepared) {
                if (ptr->AddComponentInstance(
                        std::move(value.component),
                        value.typeId) == nullptr) {
                    attached = false;
                    break;
                }
            }
            if (attached) {
                reservationStateJournal_.resize(
                    spawnCheckpoint.reservationJournalCount);
                spawnMaterializationObject_ = nullptr;
                return WorldMutationResult::Success;
            }
        }
        catch (...) {
            failure = WorldMutationResult::InternalError;
        }

        (void)ptr->SetParentInternal(nullptr);
        if (materializationActive) {
            if (!ptr->GetComponents().empty()) {
                WorldCommandCheckpoint detachCheckpoint{};
                bool detachTransaction = false;
                bool componentsDetached = false;
                try {
                    detachCheckpoint = BeginComponentCallback(*ptr);
                    detachTransaction = true;
                    const StructureReadScope structureRead =
                        AcquireStructureReadScope();
                    ptr->destroying_ = true;
                    ptr->DetachComponents();
                    componentsDetached = true;
                }
                catch (...) {
                    HIKARI_LOG_ERROR(
                        "[World] failed-spawn detach transaction failed.");
                }
                if (detachTransaction) {
                    RollbackComponentCallback(detachCheckpoint);
                }
                if (!componentsDetached) {
                    const StructureReadScope structureRead =
                        AcquireStructureReadScope();
                    ptr->destroying_ = true;
                    ptr->DetachComponents();
                }
            }
            RollbackCommandCheckpoint(spawnCheckpoint);
            spawnMaterializationObject_ = nullptr;
        }
        RemoveRenderObjectDirty(ptr);
        ptr->SetRuntimeHandle({});
        ptr->SetOwnerWorld(nullptr);
        ReleaseObjectHandle(command.object);
        if (!objects_.empty() && objects_.back().get() == ptr) {
            objects_.pop_back();
        }
        return failure;
    }

} // namespace HIKARI
