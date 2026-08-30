#include "Scene/HIKARI_World.h"

#include <array>
#include <cstring>
#include <memory>
#include <string>

#include <json.hpp>

#include "Scene/HIKARI_ComponentRegistry.h"

namespace HIKARI {

    WorldMutationResult INTERNAL::BuildWorldSpawnBatchPayload(
        const ComponentRegistry* registry,
        std::span<const WorldSpawnRequest> requests,
        std::shared_ptr<WorldSpawnBatchPayload>& outPayload) noexcept {
        outPayload.reset();
        try {
            bool hasComponentPayload = false;
            size_t totalPayloadBytes = 0u;
            for (const WorldSpawnRequest& request : requests) {
                if (request.components.size() >
                    kWorldSpawnMaximumComponentsPerObject) {
                    return WorldMutationResult::InvalidComponentData;
                }
                if (request.components.empty()) {
                    continue;
                }
                if (registry == nullptr) {
                    return WorldMutationResult::InvalidComponentType;
                }
                hasComponentPayload = true;
                std::array<const ComponentTypeInfo*,
                    kWorldSpawnMaximumComponentsPerObject> infos{};
                for (size_t componentIndex = 0u;
                    componentIndex < request.components.size();
                    ++componentIndex) {
                    const WorldSpawnComponentRequest& component =
                        request.components[componentIndex];
                    const ComponentTypeInfo* info =
                        registry->Find(component.typeId);
                    if (info == nullptr || !info->factory ||
                        info->registrationSource !=
                            ComponentRegistrationSource::GameProject) {
                        return WorldMutationResult::InvalidComponentType;
                    }
                    for (size_t prior = 0u;
                        prior < componentIndex;
                        ++prior) {
                        if (request.components[prior].typeId ==
                                component.typeId &&
                            !info->allowMultiple) {
                            return WorldMutationResult::InvalidComponentData;
                        }
                    }
                    const size_t byteCount =
                        component.initialValuesJson.size();
                    if (byteCount >
                            kWorldSpawnMaximumComponentPayloadBytes ||
                        totalPayloadBytes >
                            kWorldSpawnMaximumBatchPayloadBytes - byteCount) {
                        return WorldMutationResult::InvalidComponentData;
                    }
                    totalPayloadBytes += byteCount;
                    if (!component.initialValuesJson.empty()) {
                        const nlohmann::json values =
                            nlohmann::json::parse(
                                component.initialValuesJson.begin(),
                                component.initialValuesJson.end(),
                                nullptr,
                                false);
                        if (values.is_discarded() || !values.is_object()) {
                            return WorldMutationResult::InvalidComponentData;
                        }
                    }
                    infos[componentIndex] = info;
                }
                for (size_t componentIndex = 0u;
                    componentIndex < request.components.size();
                    ++componentIndex) {
                    const ComponentTypeInfo& info = *infos[componentIndex];
                    for (const std::string& required :
                        info.requiredComponents) {
                        const ComponentTypeId requiredId =
                            MakeComponentTypeId(required);
                        bool foundBefore = false;
                        for (size_t prior = 0u;
                            prior < componentIndex;
                            ++prior) {
                            foundBefore = foundBefore ||
                                request.components[prior].typeId == requiredId;
                        }
                        if (!foundBefore) {
                            return WorldMutationResult::InvalidComponentData;
                        }
                    }
                    for (const std::string& incompatible :
                        info.incompatibleComponents) {
                        const ComponentTypeId incompatibleId =
                            MakeComponentTypeId(incompatible);
                        for (const WorldSpawnComponentRequest& candidate :
                            request.components) {
                            if (candidate.typeId == incompatibleId) {
                                return WorldMutationResult::
                                    InvalidComponentData;
                            }
                        }
                    }
                }
            }
            if (!hasComponentPayload) {
                return WorldMutationResult::Success;
            }
            outPayload = std::make_shared<WorldSpawnBatchPayload>();
            outPayload->objects.resize(requests.size());
            for (size_t objectIndex = 0u;
                objectIndex < requests.size();
                ++objectIndex) {
                const auto components = requests[objectIndex].components;
                auto& owned = outPayload->objects[objectIndex].components;
                owned.reserve(components.size());
                for (const WorldSpawnComponentRequest& component :
                    components) {
                    owned.push_back(WorldSpawnComponentPayload{
                        component.typeId,
                        std::string(component.initialValuesJson)
                    });
                }
            }
            return WorldMutationResult::Success;
        }
        catch (...) {
            outPayload.reset();
            return WorldMutationResult::InternalError;
        }
    }

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
                spawnMaterializationObject_ = nullptr;
                componentCallbackActive_ = false;
                reservationStateJournal_.clear();
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
        spawnMaterializationObject_ = nullptr;
        componentCallbackActive_ = false;
        reservationStateJournal_.clear();
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
