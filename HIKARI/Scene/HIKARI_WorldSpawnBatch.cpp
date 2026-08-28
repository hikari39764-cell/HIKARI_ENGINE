#include "Scene/HIKARI_World.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

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
                request.name.size() >=
                    kWorldCommandObjectNameCapacity ||
                request.name.find('\0') !=
                    std::string_view::npos) {
                return rejectAll(WorldMutationResult::InvalidName);
            }
            // Batch v1 intentionally accepts only already-live parents.
            // Same-batch parent references require a separate descriptor
            // contract and are not inferred from input order.
            if (request.parent.IsValid() &&
                FindObject(request.parent) == nullptr) {
                return rejectAll(
                    WorldMutationResult::InvalidParentHandle);
            }
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

} // namespace HIKARI
