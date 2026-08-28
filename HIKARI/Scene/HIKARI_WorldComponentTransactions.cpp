#include "Scene/HIKARI_World.h"

#include <algorithm>
#include <stdexcept>

namespace HIKARI {

    World::WorldCommandCheckpoint
        World::BeginComponentCallback() {
        if (componentCallbackActive_ ||
            !CanEnterStructureCallback()) {
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
        if (checkpoint.reservationJournalCount <=
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
                if (command.kind == WorldCommandKind::Spawn &&
                    IsReservedObjectHandle(command.object)) {
                    ReleaseReservedObjectHandle(command.object);
                }
            }
            pendingCommands_.resize(
                checkpoint.pendingCommandCount);
        }
        componentCallbackActive_ = false;
    }

} // namespace HIKARI
