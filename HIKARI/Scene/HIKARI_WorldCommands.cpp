#include "Scene/HIKARI_World.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>

#include "Scene/HIKARI_GameObject.h"
#include "Scene/Internal/HIKARI_WorldCapacity.h"

namespace HIKARI {

    uint32_t World::AllocateCommandToken() noexcept {
        uint32_t token = nextCommandToken_++;
        if (token == 0u) {
            token = nextCommandToken_++;
        }
        return token;
    }

    WorldCommandSubmission World::QueueCommand(
        PendingWorldCommand command) {
        if (!IsOwnerThread()) {
            return { WorldMutationResult::WrongThread };
        }
        if (worldClosing_) {
            return { WorldMutationResult::WorldClosing };
        }
        if (pendingCommands_.size() >=
            kWorldCommandMaximumCapacity) {
            return { WorldMutationResult::CommandQueueFull };
        }

        command.sequence = nextCommandSequence_++;
        if (command.sequence == 0u) {
            command.sequence = nextCommandSequence_++;
        }
        if (command.token == 0u) {
            command.token = AllocateCommandToken();
        }
        const RuntimeObjectHandle object = command.object;
        const uint32_t token = command.token;
        try {
            pendingCommands_.push_back(std::move(command));
        }
        catch (...) {
            return {
                WorldMutationResult::InternalError,
                object,
                token
            };
        }
        return {
            WorldMutationResult::Queued,
            object,
            token
        };
    }

    WorldCommandSubmission World::SubmitSpawn(
        std::string_view name,
        const Transform3D& localTransform,
        RuntimeObjectHandle parent) {
        if (!IsOwnerThread()) {
            return { WorldMutationResult::WrongThread };
        }
        if (worldClosing_) {
            return { WorldMutationResult::WorldClosing };
        }
        if (name.empty() ||
            name.size() >= kWorldCommandObjectNameCapacity) {
            return { WorldMutationResult::InvalidName };
        }
        if (localTransform.useExplicitMatrix) {
            return {
                WorldMutationResult::TransformDecompositionFailed
            };
        }
        if (parent.IsValid() && !IsKnownObjectHandle(parent)) {
            return { WorldMutationResult::InvalidParentHandle };
        }
        if (pendingCommands_.size() >=
            kWorldCommandMaximumCapacity) {
            return { WorldMutationResult::CommandQueueFull };
        }

        const RuntimeObjectHandle object = ReserveObjectHandle();
        if (!object.IsValid()) {
            return { WorldMutationResult::InternalError };
        }

        PendingWorldCommand command{};
        command.kind = WorldCommandKind::Spawn;
        command.object = object;
        command.parent = parent;
        command.transform = WorldCommandLocalTransform{
            localTransform.position,
            localTransform.rotation,
            localTransform.scale
        };
        std::memcpy(command.name.data(), name.data(), name.size());
        command.name[name.size()] = '\0';

        const bool requiresQueue =
            !CanMutateStructure() || !pendingCommands_.empty();
        if (requiresQueue) {
            const WorldCommandSubmission submission =
                QueueCommand(std::move(command));
            if (submission.result == WorldMutationResult::Queued) {
                objectSlots_[object.slot].reservationToken =
                    submission.token;
            }
            else {
                ReleaseReservedObjectHandle(object);
            }
            return submission;
        }

        WorldMutationResult result = WorldMutationResult::InternalError;
        try {
            result = ApplyQueuedSpawn(command);
        }
        catch (...) {
            if (IsReservedObjectHandle(object)) {
                ReleaseReservedObjectHandle(object);
            }
            return {
                WorldMutationResult::InternalError,
                object,
                0u
            };
        }
        if (result != WorldMutationResult::Success &&
            IsReservedObjectHandle(object)) {
            ReleaseReservedObjectHandle(object);
        }
        return { result, object, 0u };
    }

    WorldCommandSubmission World::SubmitDestroy(
        RuntimeObjectHandle object) {
        if (!IsOwnerThread()) {
            return { WorldMutationResult::WrongThread };
        }
        if (worldClosing_) {
            return { WorldMutationResult::WorldClosing };
        }
        if (!IsKnownObjectHandle(object)) {
            return { WorldMutationResult::InvalidObjectHandle };
        }

        ObjectSlot& slot = objectSlots_[object.slot];
        if (slot.state == ObjectSlotState::Reserved) {
            if (componentCallbackActive_) {
                try {
                    reservationStateJournal_.push_back(
                        ReservedStateJournalEntry{
                            object,
                            slot.state
                        });
                }
                catch (...) {
                    return {
                        WorldMutationResult::InternalError,
                        object,
                        slot.reservationToken
                    };
                }
            }
            slot.state = ObjectSlotState::ReservedCanceled;
            return {
                WorldMutationResult::Queued,
                object,
                slot.reservationToken
            };
        }
        if (slot.state != ObjectSlotState::Live) {
            return { WorldMutationResult::InvalidObjectHandle };
        }

        if (CanMutateStructure() && pendingCommands_.empty()) {
            return {
                DestroyObject(object),
                object,
                0u
            };
        }

        PendingWorldCommand command{};
        command.kind = WorldCommandKind::Destroy;
        command.object = object;
        return QueueCommand(std::move(command));
    }

    WorldCommandSubmission World::SubmitSetParent(
        RuntimeObjectHandle object,
        RuntimeObjectHandle parent,
        WorldParentMode mode) {
        if (!IsOwnerThread()) {
            return { WorldMutationResult::WrongThread };
        }
        if (worldClosing_) {
            return { WorldMutationResult::WorldClosing };
        }
        if (mode != WorldParentMode::KeepLocal &&
            mode != WorldParentMode::KeepWorld) {
            return { WorldMutationResult::InvalidParentMode };
        }
        if (!IsKnownObjectHandle(object)) {
            return { WorldMutationResult::InvalidObjectHandle };
        }
        if (parent.IsValid() && !IsKnownObjectHandle(parent)) {
            return { WorldMutationResult::InvalidParentHandle };
        }

        const ObjectSlot& objectSlot = objectSlots_[object.slot];
        if (objectSlot.state == ObjectSlotState::ReservedCanceled) {
            return { WorldMutationResult::CommandCanceled };
        }
        if (parent.IsValid() &&
            objectSlots_[parent.slot].state ==
                ObjectSlotState::ReservedCanceled) {
            return { WorldMutationResult::InvalidParentHandle };
        }

        const bool liveObject =
            objectSlot.state == ObjectSlotState::Live;
        const bool liveParent =
            !parent.IsValid() ||
            objectSlots_[parent.slot].state == ObjectSlotState::Live;
        if (liveObject && liveParent &&
            CanMutateStructure() && pendingCommands_.empty()) {
            return {
                SetParent(object, parent, mode),
                object,
                0u
            };
        }

        PendingWorldCommand command{};
        command.kind = WorldCommandKind::SetParent;
        command.object = object;
        command.parent = parent;
        command.parentMode = mode;
        return QueueCommand(std::move(command));
    }

    WorldMutationResult World::ApplyQueuedSpawn(
        const PendingWorldCommand& command) {
        if (!command.object.IsValid() ||
            command.object.slot >= objectSlots_.size()) {
            return WorldMutationResult::InvalidObjectHandle;
        }
        ObjectSlot& slot = objectSlots_[command.object.slot];
        if (slot.generation != command.object.generation) {
            return WorldMutationResult::InvalidObjectHandle;
        }
        if (slot.state == ObjectSlotState::ReservedCanceled) {
            ReleaseReservedObjectHandle(command.object);
            return WorldMutationResult::CommandCanceled;
        }
        if (slot.state != ObjectSlotState::Reserved) {
            return WorldMutationResult::InvalidObjectHandle;
        }

        GameObject* parent = nullptr;
        if (command.parent.IsValid()) {
            parent = FindObject(command.parent);
            if (parent == nullptr) {
                ReleaseReservedObjectHandle(command.object);
                return WorldMutationResult::InvalidParentHandle;
            }
        }

        if (objects_.size() >= objects_.max_size() ||
            objects_.size() >= renderDirtyObjects_.max_size()) {
            ReleaseReservedObjectHandle(command.object);
            return WorldMutationResult::InternalError;
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
            if (parent != nullptr && !ptr->SetParent(parent)) {
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
            return WorldMutationResult::Success;
        }
        catch (...) {
            (void)ptr->SetParent(nullptr);
            RemoveRenderObjectDirty(ptr);
            ptr->SetOwnerWorld(nullptr);
            ptr->SetRuntimeHandle({});
            if (slot.state == ObjectSlotState::Live) {
                ReleaseObjectHandle(command.object);
            }
            else {
                ReleaseReservedObjectHandle(command.object);
            }
            if (objectStored &&
                !objects_.empty() &&
                objects_.back().get() == ptr) {
                objects_.pop_back();
            }
            throw;
        }
    }

    void World::RecordCommandFailure(
        const PendingWorldCommand& command,
        WorldMutationResult result,
        WorldCommandPhase phase) noexcept {
        if (commandFailures_.size() >=
            kWorldCommandFailureCapacity) {
            ++commandFailureOverflowCount_;
            return;
        }
        commandFailures_.push_back(WorldCommandFailure{
            command.token,
            command.kind,
            result,
            command.object,
            command.parent,
            phase
        });
    }

    uint32_t World::FlushQueuedCommands(WorldCommandPhase phase) {
        if (!IsOwnerThread() ||
            worldClosing_ ||
            structureReadDepth_ != 0u ||
            commandFlushActive_ ||
            pendingCommands_.empty()) {
            return 0u;
        }

        pendingCommands_.swap(executionCommands_);
        commandFlushActive_ = true;
        uint32_t executedCount = 0u;
        for (const PendingWorldCommand& command : executionCommands_) {
            WorldMutationResult result = WorldMutationResult::InternalError;
            try {
                switch (command.kind) {
                case WorldCommandKind::Spawn:
                    result = ApplyQueuedSpawn(command);
                    break;
                case WorldCommandKind::Destroy:
                    commandApplyActive_ = true;
                    result = DestroyObject(command.object);
                    commandApplyActive_ = false;
                    break;
                case WorldCommandKind::SetParent:
                    commandApplyActive_ = true;
                    result = SetParent(
                        command.object,
                        command.parent,
                        command.parentMode);
                    commandApplyActive_ = false;
                    break;
                default:
                    result = WorldMutationResult::InternalError;
                    break;
                }
            }
            catch (...) {
                commandApplyActive_ = false;
                if (command.kind == WorldCommandKind::Spawn &&
                    IsReservedObjectHandle(command.object)) {
                    ReleaseReservedObjectHandle(command.object);
                }
                result = WorldMutationResult::InternalError;
            }
            if (result != WorldMutationResult::Success) {
                RecordCommandFailure(command, result, phase);
            }
            ++executedCount;
        }
        commandApplyActive_ = false;
        commandFlushActive_ = false;
        executionCommands_.clear();
        return executedCount;
    }

    uint32_t World::GetCommandFailureCount() const noexcept {
        return IsOwnerThread()
            ? static_cast<uint32_t>(commandFailures_.size())
            : 0u;
    }

    bool World::ReadCommandFailure(
        uint32_t index,
        WorldCommandFailure& outFailure) const noexcept {
        if (!IsOwnerThread() || index >= commandFailures_.size()) {
            return false;
        }
        outFailure = commandFailures_[index];
        return true;
    }

    uint32_t World::GetCommandFailureOverflowCount() const noexcept {
        return IsOwnerThread()
            ? commandFailureOverflowCount_
            : 0u;
    }

} // namespace HIKARI
