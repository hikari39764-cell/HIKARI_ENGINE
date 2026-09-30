#include "Scene/HIKARI_World.h"

#include <limits>
#include <stdexcept>
#include <utility>

#include "Scene/HIKARI_GameObject.h"
#include "Scene/Internal/HIKARI_WorldCapacity.h"

namespace HIKARI {

const std::unique_ptr<GameObject>& World::LiveObjectRange::Iterator::operator*() const noexcept {
    static const std::unique_ptr<GameObject> empty{};
    if (world_ == nullptr || !world_->IsOwnerThread() || slot_ == kInvalidLiveObjectSlot ||
        slot_ >= world_->objectSlots_.size()) {
        return empty;
    }
    const ObjectSlot& slot = world_->objectSlots_[slot_];
    return slot.storageIndex < world_->objects_.size() ? world_->objects_[slot.storageIndex] : empty;
}

World::LiveObjectRange::Iterator& World::LiveObjectRange::Iterator::operator++() noexcept {
    if (world_ == nullptr || !world_->IsOwnerThread() || slot_ == kInvalidLiveObjectSlot ||
        slot_ >= world_->objectSlots_.size()) {
        world_ = nullptr;
        slot_ = kInvalidLiveObjectSlot;
        return *this;
    }
    slot_ = world_->objectSlots_[slot_].nextLiveSlot;
    return *this;
}

bool World::LiveObjectRange::Iterator::operator==(const Iterator& other) const noexcept {
    return world_ == other.world_ && slot_ == other.slot_;
}

bool World::LiveObjectRange::Iterator::operator!=(const Iterator& other) const noexcept { return !(*this == other); }

World::LiveObjectRange::Iterator World::LiveObjectRange::begin() const noexcept {
    const World* iterationWorld = world_ != nullptr && world_->IsOwnerThread() ? world_ : nullptr;
    return Iterator(iterationWorld,
                    iterationWorld != nullptr ? iterationWorld->firstLiveObjectSlot_ : kInvalidLiveObjectSlot);
}

World::LiveObjectRange::Iterator World::LiveObjectRange::end() const noexcept {
    return Iterator(world_ != nullptr && world_->IsOwnerThread() ? world_ : nullptr, kInvalidLiveObjectSlot);
}

size_t World::LiveObjectRange::size() const noexcept {
    return world_ != nullptr && world_->IsOwnerThread() ? world_->liveObjectCount_ : 0u;
}

bool World::LiveObjectRange::empty() const noexcept { return size() == 0u; }

GameObject* World::FindObject(RuntimeObjectHandle handle) noexcept {
    if (!IsOwnerThread()) {
        return nullptr;
    }
    if (!handle.IsValid() || handle.slot >= objectSlots_.size()) {
        return nullptr;
    }
    const ObjectSlot& slot = objectSlots_[handle.slot];
    return slot.generation == handle.generation && slot.state == ObjectSlotState::Live &&
                   slot.storageKind == ObjectStorageKind::GameObject
               ? slot.object
               : nullptr;
}

const GameObject* World::FindObject(RuntimeObjectHandle handle) const noexcept {
    return const_cast<World*>(this)->FindObject(handle);
}

RuntimeObjectHandle World::ReserveObjectHandle() {
    if (!freeObjectSlots_.empty()) {
        const uint32_t slotIndex = freeObjectSlots_.back();
        freeObjectSlots_.pop_back();
        ObjectSlot& slot = objectSlots_[slotIndex];
        if (slot.state != ObjectSlotState::Free || slot.object != nullptr ||
            slot.storageKind != ObjectStorageKind::None || slot.massTypeId.IsValid() ||
            slot.storageIndex != kInvalidObjectStorageIndex || slot.prefabInstanceId != 0u ||
            slot.prefabLocalObjectId != 0u) {
            return {};
        }
        slot.reservationToken = 0u;
        slot.state = ObjectSlotState::Reserved;
        return RuntimeObjectHandle{slotIndex, slot.generation};
    }

    if (objectSlots_.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
        return {};
    }
    const uint32_t slotIndex = static_cast<uint32_t>(objectSlots_.size());
    const size_t nextSlotCount = objectSlots_.size() + 1u;
    const size_t slotCapacity =
        INTERNAL::GrowWorldCapacity((std::max)(objectSlots_.capacity(), freeObjectSlots_.capacity()), nextSlotCount,
                                    (std::min)(static_cast<size_t>((std::numeric_limits<uint32_t>::max)()),
                                               (std::min)(objectSlots_.max_size(), freeObjectSlots_.max_size())));
    objectSlots_.reserve(slotCapacity);
    freeObjectSlots_.reserve(slotCapacity);
    ObjectSlot slot{};
    slot.state = ObjectSlotState::Reserved;
    objectSlots_.push_back(std::move(slot));
    return RuntimeObjectHandle{slotIndex, 1u};
}

bool World::ReserveObjectHandleBatch(std::span<RuntimeObjectHandle> outHandles) noexcept {
    if (outHandles.empty()) {
        return true;
    }
    size_t reservedCount = 0u;
    try {
        const size_t newSlotCount =
            outHandles.size() > freeObjectSlots_.size() ? outHandles.size() - freeObjectSlots_.size() : 0u;
        if (newSlotCount > static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) - objectSlots_.size()) {
            return false;
        }
        const size_t nextSlotCount = objectSlots_.size() + newSlotCount;
        const size_t slotCapacity =
            INTERNAL::GrowWorldCapacity((std::max)(objectSlots_.capacity(), freeObjectSlots_.capacity()), nextSlotCount,
                                        (std::min)(static_cast<size_t>((std::numeric_limits<uint32_t>::max)()),
                                                   (std::min)(objectSlots_.max_size(), freeObjectSlots_.max_size())));
        objectSlots_.reserve(slotCapacity);
        freeObjectSlots_.reserve(slotCapacity);
        for (; reservedCount < outHandles.size(); ++reservedCount) {
            outHandles[reservedCount] = ReserveObjectHandle();
            if (!outHandles[reservedCount].IsValid()) {
                throw std::runtime_error("World batch handle reservation failed.");
            }
        }
        return true;
    } catch (...) {
        for (size_t index = 0u; index < reservedCount; ++index) {
            ReleaseReservedObjectHandle(outHandles[index]);
            outHandles[index] = {};
        }
        return false;
    }
}

bool World::CommitReservedObjectHandle(RuntimeObjectHandle handle, GameObject* object, size_t storageIndex) noexcept {
    if (!handle.IsValid() || object == nullptr || storageIndex == kInvalidObjectStorageIndex ||
        handle.slot >= objectSlots_.size()) {
        return false;
    }
    ObjectSlot& slot = objectSlots_[handle.slot];
    if (slot.generation != handle.generation || slot.state != ObjectSlotState::Reserved || slot.object != nullptr ||
        slot.storageKind != ObjectStorageKind::None || slot.massTypeId.IsValid()) {
        return false;
    }
    slot.object = object;
    slot.storageIndex = storageIndex;
    slot.massTypeId = {};
    slot.reservationToken = 0u;
    slot.state = ObjectSlotState::Live;
    slot.storageKind = ObjectStorageKind::GameObject;
    AppendLiveObjectSlot(handle.slot);
    return true;
}

bool World::CommitReservedMassObjectHandle(RuntimeObjectHandle handle, ComponentTypeId typeId,
                                           size_t storageIndex) noexcept {
    if (!handle.IsValid() || !typeId.IsValid() || storageIndex == kInvalidObjectStorageIndex ||
        handle.slot >= objectSlots_.size()) {
        return false;
    }
    ObjectSlot& slot = objectSlots_[handle.slot];
    if (slot.generation != handle.generation || slot.state != ObjectSlotState::Reserved || slot.object != nullptr ||
        slot.storageKind != ObjectStorageKind::None) {
        return false;
    }
    slot.storageIndex = storageIndex;
    slot.massTypeId = typeId;
    slot.reservationToken = 0u;
    slot.state = ObjectSlotState::Live;
    slot.storageKind = ObjectStorageKind::Mass;
    return true;
}

void World::ReleaseReservedObjectHandle(RuntimeObjectHandle handle) noexcept {
    if (!handle.IsValid() || handle.slot >= objectSlots_.size()) {
        return;
    }
    const ObjectSlot& slot = objectSlots_[handle.slot];
    if (slot.generation != handle.generation ||
        (slot.state != ObjectSlotState::Reserved && slot.state != ObjectSlotState::ReservedCanceled) ||
        slot.object != nullptr || slot.storageKind != ObjectStorageKind::None) {
        return;
    }
    RecycleObjectSlot(handle.slot);
}

bool World::IsReservedObjectHandle(RuntimeObjectHandle handle) const noexcept {
    if (!handle.IsValid() || handle.slot >= objectSlots_.size()) {
        return false;
    }
    const ObjectSlot& slot = objectSlots_[handle.slot];
    return slot.generation == handle.generation &&
           (slot.state == ObjectSlotState::Reserved || slot.state == ObjectSlotState::ReservedCanceled) &&
           slot.object == nullptr;
}

bool World::IsKnownObjectHandle(RuntimeObjectHandle handle) const noexcept {
    if (!handle.IsValid() || handle.slot >= objectSlots_.size()) {
        return false;
    }
    const ObjectSlot& slot = objectSlots_[handle.slot];
    return slot.generation == handle.generation &&
           (slot.state == ObjectSlotState::Reserved || slot.state == ObjectSlotState::Live);
}

bool World::TryGetMassObjectLocation(RuntimeObjectHandle handle, ComponentTypeId& outTypeId,
                                     size_t& outStorageIndex) const noexcept {
    outTypeId = {};
    outStorageIndex = kInvalidObjectStorageIndex;
    if (!handle.IsValid() || handle.slot >= objectSlots_.size()) {
        return false;
    }
    const ObjectSlot& slot = objectSlots_[handle.slot];
    if (slot.generation != handle.generation || slot.state != ObjectSlotState::Live ||
        slot.storageKind != ObjectStorageKind::Mass || slot.object != nullptr || !slot.massTypeId.IsValid() ||
        slot.storageIndex == kInvalidObjectStorageIndex) {
        return false;
    }
    outTypeId = slot.massTypeId;
    outStorageIndex = slot.storageIndex;
    return true;
}

void World::RecycleObjectSlot(uint32_t slotIndex) noexcept {
    if (slotIndex >= objectSlots_.size()) {
        return;
    }
    ObjectSlot& slot = objectSlots_[slotIndex];
    slot.object = nullptr;
    slot.storageIndex = kInvalidObjectStorageIndex;
    slot.massTypeId = {};
    slot.previousLiveSlot = kInvalidLiveObjectSlot;
    slot.nextLiveSlot = kInvalidLiveObjectSlot;
    slot.reservationToken = 0u;
    slot.prefabInstanceId = 0u;
    slot.prefabLocalObjectId = 0u;
    slot.storageKind = ObjectStorageKind::None;
    if (slot.generation == (std::numeric_limits<uint32_t>::max)()) {
        slot.state = ObjectSlotState::Retired;
        return;
    }
    ++slot.generation;
    slot.state = ObjectSlotState::Free;
    freeObjectSlots_.push_back(slotIndex);
}

void World::AppendLiveObjectSlot(uint32_t slotIndex) noexcept {
    ObjectSlot& slot = objectSlots_[slotIndex];
    slot.previousLiveSlot = lastLiveObjectSlot_;
    slot.nextLiveSlot = kInvalidLiveObjectSlot;
    if (lastLiveObjectSlot_ != kInvalidLiveObjectSlot) {
        objectSlots_[lastLiveObjectSlot_].nextLiveSlot = slotIndex;
    } else {
        firstLiveObjectSlot_ = slotIndex;
    }
    lastLiveObjectSlot_ = slotIndex;
    ++liveObjectCount_;
}

void World::RemoveLiveObjectSlot(uint32_t slotIndex) noexcept {
    ObjectSlot& slot = objectSlots_[slotIndex];
    if (slot.previousLiveSlot != kInvalidLiveObjectSlot) {
        objectSlots_[slot.previousLiveSlot].nextLiveSlot = slot.nextLiveSlot;
    } else {
        firstLiveObjectSlot_ = slot.nextLiveSlot;
    }
    if (slot.nextLiveSlot != kInvalidLiveObjectSlot) {
        objectSlots_[slot.nextLiveSlot].previousLiveSlot = slot.previousLiveSlot;
    } else {
        lastLiveObjectSlot_ = slot.previousLiveSlot;
    }
    slot.previousLiveSlot = kInvalidLiveObjectSlot;
    slot.nextLiveSlot = kInvalidLiveObjectSlot;
    if (liveObjectCount_ > 0u) {
        --liveObjectCount_;
    }
}

void World::ReleaseObjectHandle(RuntimeObjectHandle handle) {
    if (!handle.IsValid() || handle.slot >= objectSlots_.size()) {
        return;
    }
    ObjectSlot& slot = objectSlots_[handle.slot];
    if (slot.generation != handle.generation || slot.state != ObjectSlotState::Live ||
        slot.storageKind != ObjectStorageKind::GameObject || slot.object == nullptr) {
        return;
    }
    ReleasePrefabInstanceObject(slot.prefabInstanceId, slot.prefabLocalObjectId);
    RemoveLiveObjectSlot(handle.slot);
    RecycleObjectSlot(handle.slot);
}

} // namespace HIKARI
