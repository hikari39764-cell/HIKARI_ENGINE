#include "Scene/HIKARI_World.h"

#include <algorithm>
#include <array>
#include <limits>

#include "Scene/Internal/HIKARI_WorldCapacity.h"

namespace HIKARI {
namespace {

    GameProjectTransform ToMassTransform(
        const Transform3D& value) noexcept {
        return GameProjectTransform{
            { value.position.x, value.position.y, value.position.z },
            {
                value.rotation.x,
                value.rotation.y,
                value.rotation.z,
                value.rotation.w
            },
            { value.scale.x, value.scale.y, value.scale.z }
        };
    }

    Transform3D FromMassTransform(
        const GameProjectTransform& value) noexcept {
        Transform3D result{};
        result.position = {
            value.position.x,
            value.position.y,
            value.position.z
        };
        result.rotation = {
            value.rotation.x,
            value.rotation.y,
            value.rotation.z,
            value.rotation.w
        };
        result.scale = {
            value.scale.x,
            value.scale.y,
            value.scale.z
        };
        return result;
    }

} // namespace

    WorldMutationResult World::SubmitMassSpawnBatch(
        const WorldMassSpawnBatchRequest& request,
        std::span<WorldCommandSubmission> outSubmissions) {
        const auto rejectAll = [&](WorldMutationResult result) noexcept {
            for (WorldCommandSubmission& submission : outSubmissions) {
                submission = WorldCommandSubmission{ result };
            }
            return result;
        };

        const size_t count = request.transforms.size();
        if (outSubmissions.size() != count ||
            count == 0u ||
            count > kWorldMassSpawnBatchMaximumCapacity) {
            return rejectAll(WorldMutationResult::MutationDisallowed);
        }
        if (!IsOwnerThread()) {
            return rejectAll(WorldMutationResult::WrongThread);
        }
        if (worldClosing_) {
            return rejectAll(WorldMutationResult::WorldClosing);
        }
        if (pendingCommands_.size() >= kWorldCommandMaximumCapacity) {
            return rejectAll(WorldMutationResult::CommandQueueFull);
        }
        const WorldMassSchema* registeredSchema =
            massSchemaRegistry_ != nullptr
            ? massSchemaRegistry_->Find(request.schema.typeId)
            : nullptr;
        if (registeredSchema == nullptr ||
            !IsSameWorldMassSchema(*registeredSchema, request.schema) ||
            !massStore_.CanUseSchema(request.schema)) {
            return rejectAll(WorldMutationResult::InvalidComponentData);
        }

        std::shared_ptr<WorldMassSpawnBatchPayload> payload{};
        const WorldMutationResult payloadResult =
            INTERNAL::BuildWorldMassSpawnBatchPayload(request, payload);
        if (payloadResult != WorldMutationResult::Success ||
            payload == nullptr) {
            return rejectAll(payloadResult);
        }

        const size_t previousCommandCount = pendingCommands_.size();
        try {
            pendingCommands_.reserve(INTERNAL::GrowWorldCapacity(
                pendingCommands_.capacity(),
                previousCommandCount + 1u,
                kWorldCommandMaximumCapacity));
        }
        catch (...) {
            return rejectAll(WorldMutationResult::InternalError);
        }

        std::array<RuntimeObjectHandle,
            kWorldMassSpawnBatchMaximumCapacity> reservedHandles{};
        const std::span reservedSpan{
            reservedHandles.data(),
            count
        };
        if (!ReserveObjectHandleBatch(reservedSpan)) {
            return rejectAll(WorldMutationResult::InternalError);
        }

        try {
            payload->objects.assign(reservedSpan.begin(), reservedSpan.end());
            PendingWorldCommand command{};
            command.sequence = nextCommandSequence_++;
            if (command.sequence == 0u) {
                command.sequence = nextCommandSequence_++;
            }
            command.token = AllocateCommandToken();
            command.kind = WorldCommandKind::SpawnMass;
            command.object = reservedHandles[0];
            command.payload = payload;
            pendingCommands_.push_back(command);
            for (size_t index = 0u; index < count; ++index) {
                objectSlots_[reservedHandles[index].slot].reservationToken =
                    command.token;
                outSubmissions[index] = WorldCommandSubmission{
                    WorldMutationResult::Queued,
                    reservedHandles[index],
                    command.token
                };
            }
        }
        catch (...) {
            pendingCommands_.resize(previousCommandCount);
            for (size_t index = 0u; index < count; ++index) {
                ReleaseReservedObjectHandle(reservedHandles[index]);
            }
            return rejectAll(WorldMutationResult::InternalError);
        }
        return WorldMutationResult::Queued;
    }

    WorldMutationResult World::SubmitMassDestroyBatch(
        std::span<const RuntimeObjectHandle> objects,
        std::span<WorldCommandSubmission> outSubmissions) {
        const auto rejectAll = [&](WorldMutationResult result) noexcept {
            for (WorldCommandSubmission& submission : outSubmissions) {
                submission = WorldCommandSubmission{ result };
            }
            return result;
        };
        if (objects.empty() || objects.size() != outSubmissions.size() ||
            objects.size() > kWorldMassSpawnBatchMaximumCapacity) {
            return rejectAll(WorldMutationResult::MutationDisallowed);
        }
        if (!IsOwnerThread()) {
            return rejectAll(WorldMutationResult::WrongThread);
        }
        if (worldClosing_) {
            return rejectAll(WorldMutationResult::WorldClosing);
        }
        if (pendingCommands_.size() >= kWorldCommandMaximumCapacity) {
            return rejectAll(WorldMutationResult::CommandQueueFull);
        }
        std::array<uint64_t,
            kWorldMassSpawnBatchMaximumCapacity> objectIdentities{};
        for (size_t index = 0u; index < objects.size(); ++index) {
            ComponentTypeId typeId{};
            size_t storageIndex = 0u;
            if (!TryGetMassObjectLocation(
                    objects[index],
                    typeId,
                    storageIndex)) {
                return rejectAll(
                    WorldMutationResult::InvalidObjectHandle);
            }
            objectIdentities[index] = objects[index].ToValue();
        }
        const auto identityEnd =
            objectIdentities.begin() + objects.size();
        std::sort(objectIdentities.begin(), identityEnd);
        if (std::adjacent_find(
                objectIdentities.begin(), identityEnd) != identityEnd) {
            return rejectAll(WorldMutationResult::InvalidObjectHandle);
        }

        try {
            auto payload =
                std::make_shared<WorldMassDestroyBatchPayload>();
            payload->objects.assign(objects.begin(), objects.end());
            pendingCommands_.reserve(INTERNAL::GrowWorldCapacity(
                pendingCommands_.capacity(),
                pendingCommands_.size() + 1u,
                kWorldCommandMaximumCapacity));
            PendingWorldCommand command{};
            command.sequence = nextCommandSequence_++;
            if (command.sequence == 0u) {
                command.sequence = nextCommandSequence_++;
            }
            command.token = AllocateCommandToken();
            command.kind = WorldCommandKind::DestroyMass;
            command.object = objects.front();
            command.payload = std::move(payload);
            pendingCommands_.push_back(command);
            for (size_t index = 0u; index < objects.size(); ++index) {
                outSubmissions[index] = WorldCommandSubmission{
                    WorldMutationResult::Queued,
                    objects[index],
                    command.token
                };
            }
            return WorldMutationResult::Queued;
        }
        catch (...) {
            return rejectAll(WorldMutationResult::InternalError);
        }
    }

    WorldMutationResult World::ApplyQueuedMassSpawnBatch(
        const PendingWorldCommand& command) {
        if (command.payload == nullptr ||
            command.payload->kind != WorldCommandPayloadKind::SpawnMass) {
            return WorldMutationResult::InvalidObjectHandle;
        }

        const auto* massPayload = static_cast<
            const WorldMassSpawnBatchPayload*>(command.payload.get());
        const WorldMassSpawnBatchPayload& payload = *massPayload;
        const size_t count = payload.objects.size();
        if (count == 0u ||
            payload.transforms.size() != count ||
            payload.velocities.size() != count ||
            payload.podStride == 0u ||
            count >
                (std::numeric_limits<size_t>::max)() / payload.podStride ||
            count * payload.podStride > payload.podBytes.size()) {
            return WorldMutationResult::InvalidComponentData;
        }

        std::array<uint32_t,
            kWorldMassSpawnBatchMaximumCapacity> sourceIndices{};
        std::array<RuntimeObjectHandle,
            kWorldMassSpawnBatchMaximumCapacity> activeObjects{};
        size_t activeCount = 0u;
        for (size_t index = 0u; index < count; ++index) {
            const RuntimeObjectHandle object = payload.objects[index];
            if (!object.IsValid() || object.slot >= objectSlots_.size()) {
                return WorldMutationResult::InvalidObjectHandle;
            }
            const ObjectSlot& slot = objectSlots_[object.slot];
            if (slot.generation != object.generation ||
                slot.storageKind != ObjectStorageKind::None) {
                return WorldMutationResult::InvalidObjectHandle;
            }
            if (slot.state == ObjectSlotState::ReservedCanceled) {
                ReleaseReservedObjectHandle(object);
                continue;
            }
            if (slot.state != ObjectSlotState::Reserved) {
                return WorldMutationResult::InvalidObjectHandle;
            }
            sourceIndices[activeCount] = static_cast<uint32_t>(index);
            activeObjects[activeCount] = object;
            ++activeCount;
        }
        if (activeCount == 0u) {
            return WorldMutationResult::CommandCanceled;
        }

        const std::span activeObjectSpan{
            activeObjects.data(),
            activeCount
        };
        const std::span sourceIndexSpan{
            sourceIndices.data(),
            activeCount
        };
        size_t firstRowIndex = kInvalidObjectStorageIndex;
        bool appended = false;
        try {
            appended = massStore_.AppendBatch(
                payload.schema,
                massSchemaRegistry_ != nullptr
                    ? massSchemaRegistry_->FindRenderProxy(
                        payload.schema.typeId)
                    : nullptr,
                activeObjectSpan,
                payload.transforms,
                payload.velocities,
                payload.podBytes.data(),
                payload.podStride,
                sourceIndexSpan,
                firstRowIndex);
        }
        catch (...) {
            for (const RuntimeObjectHandle object : activeObjectSpan) {
                if (IsReservedObjectHandle(object)) {
                    ReleaseReservedObjectHandle(object);
                }
            }
            return WorldMutationResult::InternalError;
        }
        if (!appended) {
            for (const RuntimeObjectHandle object : activeObjectSpan) {
                if (IsReservedObjectHandle(object)) {
                    ReleaseReservedObjectHandle(object);
                }
            }
            return WorldMutationResult::InternalError;
        }

        size_t committedCount = 0u;
        for (; committedCount < activeCount; ++committedCount) {
            if (!CommitReservedMassObjectHandle(
                    activeObjects[committedCount],
                    payload.schema.typeId,
                    firstRowIndex + committedCount)) {
                break;
            }
        }
        if (committedCount == activeCount) {
            return WorldMutationResult::Success;
        }

        const bool rowsRemoved = massStore_.RemoveBackBatch(
            payload.schema,
            firstRowIndex,
            activeObjectSpan);
        if (rowsRemoved) {
            for (size_t index = 0u; index < committedCount; ++index) {
                RecycleObjectSlot(activeObjects[index].slot);
            }
        }
        for (size_t index = committedCount; index < activeCount; ++index) {
            if (IsReservedObjectHandle(activeObjects[index])) {
                ReleaseReservedObjectHandle(activeObjects[index]);
            }
        }
        return WorldMutationResult::InternalError;
    }

    WorldMutationResult World::ApplyQueuedMassDestroyBatch(
        const PendingWorldCommand& command) {
        if (command.payload == nullptr ||
            command.payload->kind != WorldCommandPayloadKind::DestroyMass) {
            return WorldMutationResult::InvalidObjectHandle;
        }
        const auto* payload = static_cast<
            const WorldMassDestroyBatchPayload*>(command.payload.get());
        if (payload->objects.empty()) {
            return WorldMutationResult::InvalidObjectHandle;
        }
        for (const RuntimeObjectHandle object :
            payload->objects) {
            ComponentTypeId typeId{};
            size_t storageIndex = 0u;
            if (!TryGetMassObjectLocation(
                    object,
                    typeId,
                    storageIndex)) {
                return WorldMutationResult::InvalidObjectHandle;
            }
        }
        for (const RuntimeObjectHandle object :
            payload->objects) {
            const WorldMutationResult result = DestroyObject(object);
            if (result != WorldMutationResult::Success) {
                return result;
            }
        }
        return WorldMutationResult::Success;
    }

    bool World::VisitMassObjects(
        ComponentTypeId typeId,
        void* context,
        MassObjectVisitorFn visitor) noexcept {
        if (!IsOwnerThread() || worldClosing_ ||
            !typeId.IsValid() || visitor == nullptr) {
            return false;
        }
        const StructureReadScope structureRead =
            AcquireStructureReadScope();
        struct VisitContext {
            World* world = nullptr;
            ComponentTypeId typeId{};
            void* userContext = nullptr;
            MassObjectVisitorFn visitor = nullptr;
        } visit{ this, typeId, context, visitor };
        return massStore_.VisitMutable(
            typeId,
            &visit,
            [](void* rawContext,
                size_t rowIndex,
                WorldMassMutableRow& row) noexcept {
                VisitContext& current =
                    *static_cast<VisitContext*>(rawContext);
                ComponentTypeId slotTypeId{};
                size_t slotStorageIndex = 0u;
                return current.world->TryGetMassObjectLocation(
                        row.object,
                        slotTypeId,
                        slotStorageIndex) &&
                    slotTypeId == current.typeId &&
                    slotStorageIndex == rowIndex &&
                    current.visitor(current.userContext, row);
            });
    }

    WorldMutationResult World::SetMassTransformIfPresent(
        RuntimeObjectHandle object,
        const Transform3D& transform,
        bool& outIsMassObject) noexcept {
        ComponentTypeId typeId{};
        size_t storageIndex = 0u;
        outIsMassObject = TryGetMassObjectLocation(
            object,
            typeId,
            storageIndex);
        if (!outIsMassObject) {
            return WorldMutationResult::InvalidObjectHandle;
        }
        if (transform.useExplicitMatrix) {
            return WorldMutationResult::TransformDecompositionFailed;
        }
        const GameProjectTransform massTransform =
            ToMassTransform(transform);
        if (!INTERNAL::IsValidWorldMassTransform(
                massTransform.position,
                massTransform.rotation,
                massTransform.scale)) {
            return WorldMutationResult::TransformDecompositionFailed;
        }
        return massStore_.SetTransform(
            typeId,
            storageIndex,
            object,
            massTransform)
            ? WorldMutationResult::Success
            : WorldMutationResult::InternalError;
    }

    bool World::TryGetMassTransformIfPresent(
        RuntimeObjectHandle object,
        Transform3D& outTransform,
        bool& outIsMassObject) const noexcept {
        ComponentTypeId typeId{};
        size_t storageIndex = 0u;
        outIsMassObject = TryGetMassObjectLocation(
            object,
            typeId,
            storageIndex);
        if (!outIsMassObject) {
            return false;
        }
        GameProjectTransform transform{};
        if (!massStore_.TryGetTransform(
                typeId,
                storageIndex,
                object,
                transform)) {
            return false;
        }
        outTransform = FromMassTransform(transform);
        return true;
    }

    bool World::PublishMassRenderProxies(
        uint64_t frameIndex,
        void* context,
        WorldMassStore::RenderPublicationVisitorFn visitor,
        WorldMassRenderPublicationReceipt& outReceipt) noexcept {
        outReceipt = {};
        if (!IsOwnerThread() || worldClosing_ || visitor == nullptr) {
            return false;
        }
        const StructureReadScope structureRead =
            AcquireStructureReadScope();
        return massStore_.PublishRenderStores(
            runtimeIdentity_,
            frameIndex,
            context,
            visitor,
            outReceipt);
    }

    bool World::AcknowledgeMassRenderProxies(
        const WorldMassRenderPublicationReceipt& receipt) noexcept {
        return IsOwnerThread() && !worldClosing_ &&
            receipt.worldIdentity == runtimeIdentity_ &&
            massStore_.AcknowledgeRenderStores(receipt);
    }

} // namespace HIKARI
