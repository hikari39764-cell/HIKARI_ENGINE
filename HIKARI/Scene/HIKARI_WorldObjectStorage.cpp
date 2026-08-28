#include "Scene/HIKARI_World.h"

#include <limits>

#include "Scene/HIKARI_GameObject.h"
#include "Scene/Internal/HIKARI_WorldCapacity.h"

namespace HIKARI {

    GameObject* World::FindObject(
        RuntimeObjectHandle handle) noexcept {
        if (!IsOwnerThread()) {
            return nullptr;
        }
        if (!handle.IsValid() || handle.slot >= objectSlots_.size()) {
            return nullptr;
        }
        const ObjectSlot& slot = objectSlots_[handle.slot];
        return slot.generation == handle.generation &&
            slot.state == ObjectSlotState::Live
            ? slot.object
            : nullptr;
    }

    const GameObject* World::FindObject(
        RuntimeObjectHandle handle) const noexcept {
        return const_cast<World*>(this)->FindObject(handle);
    }

    RuntimeObjectHandle World::AllocateObjectHandle(
        GameObject* object) {
        const RuntimeObjectHandle handle = ReserveObjectHandle();
        if (!handle.IsValid()) {
            return {};
        }
        if (!CommitReservedObjectHandle(handle, object)) {
            ReleaseReservedObjectHandle(handle);
            return {};
        }
        return handle;
    }

    RuntimeObjectHandle World::ReserveObjectHandle() {
        if (!freeObjectSlots_.empty()) {
            const uint32_t slotIndex = freeObjectSlots_.back();
            freeObjectSlots_.pop_back();
            ObjectSlot& slot = objectSlots_[slotIndex];
            if (slot.state != ObjectSlotState::Free ||
                slot.object != nullptr) {
                return {};
            }
            slot.reservationToken = 0u;
            slot.state = ObjectSlotState::Reserved;
            return RuntimeObjectHandle{
                slotIndex,
                slot.generation
            };
        }

        if (objectSlots_.size() >=
            static_cast<size_t>(
                (std::numeric_limits<uint32_t>::max)())) {
            return {};
        }
        const uint32_t slotIndex =
            static_cast<uint32_t>(objectSlots_.size());
        const size_t nextSlotCount = objectSlots_.size() + 1u;
        const size_t slotCapacity = INTERNAL::GrowWorldCapacity(
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
        objectSlots_.push_back(ObjectSlot{
            nullptr,
            1u,
            0u,
            ObjectSlotState::Reserved
        });
        return RuntimeObjectHandle{ slotIndex, 1u };
    }

    bool World::CommitReservedObjectHandle(
        RuntimeObjectHandle handle,
        GameObject* object) noexcept {
        if (!handle.IsValid() ||
            object == nullptr ||
            handle.slot >= objectSlots_.size()) {
            return false;
        }
        ObjectSlot& slot = objectSlots_[handle.slot];
        if (slot.generation != handle.generation ||
            slot.state != ObjectSlotState::Reserved ||
            slot.object != nullptr) {
            return false;
        }
        slot.object = object;
        slot.reservationToken = 0u;
        slot.state = ObjectSlotState::Live;
        return true;
    }

    void World::ReleaseReservedObjectHandle(
        RuntimeObjectHandle handle) noexcept {
        if (!handle.IsValid() || handle.slot >= objectSlots_.size()) {
            return;
        }
        const ObjectSlot& slot = objectSlots_[handle.slot];
        if (slot.generation != handle.generation ||
            (slot.state != ObjectSlotState::Reserved &&
             slot.state != ObjectSlotState::ReservedCanceled) ||
            slot.object != nullptr) {
            return;
        }
        RecycleObjectSlot(handle.slot);
    }

    bool World::IsReservedObjectHandle(
        RuntimeObjectHandle handle) const noexcept {
        if (!handle.IsValid() || handle.slot >= objectSlots_.size()) {
            return false;
        }
        const ObjectSlot& slot = objectSlots_[handle.slot];
        return slot.generation == handle.generation &&
            (slot.state == ObjectSlotState::Reserved ||
             slot.state == ObjectSlotState::ReservedCanceled) &&
            slot.object == nullptr;
    }

    bool World::IsKnownObjectHandle(
        RuntimeObjectHandle handle) const noexcept {
        if (!handle.IsValid() || handle.slot >= objectSlots_.size()) {
            return false;
        }
        const ObjectSlot& slot = objectSlots_[handle.slot];
        return slot.generation == handle.generation &&
            (slot.state == ObjectSlotState::Reserved ||
             slot.state == ObjectSlotState::Live);
    }

    void World::RecycleObjectSlot(uint32_t slotIndex) noexcept {
        if (slotIndex >= objectSlots_.size()) {
            return;
        }
        ObjectSlot& slot = objectSlots_[slotIndex];
        slot.object = nullptr;
        slot.reservationToken = 0u;
        if (slot.generation ==
            (std::numeric_limits<uint32_t>::max)()) {
            slot.state = ObjectSlotState::Retired;
            return;
        }
        ++slot.generation;
        slot.state = ObjectSlotState::Free;
        freeObjectSlots_.push_back(slotIndex);
    }

    void World::ReleaseObjectHandle(RuntimeObjectHandle handle) {
        if (!handle.IsValid() || handle.slot >= objectSlots_.size()) {
            return;
        }
        ObjectSlot& slot = objectSlots_[handle.slot];
        if (slot.generation != handle.generation ||
            slot.state != ObjectSlotState::Live ||
            slot.object == nullptr) {
            return;
        }
        RecycleObjectSlot(handle.slot);
    }

} // namespace HIKARI
