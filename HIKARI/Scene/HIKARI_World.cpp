#include "Scene/HIKARI_World.h"

#include <algorithm>
#include <stdexcept>

#include "Core/HIKARI_Logger.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/Internal/HIKARI_WorldCapacity.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_WorldEvents.h"

namespace HIKARI {

    GameObject* World::CreateObject(const std::string& name) {
        if (!CanMutateStructure()) {
            return nullptr;
        }
        if (objects_.size() >= objects_.max_size() ||
            objects_.size() >= renderDirtyObjects_.max_size()) {
            return nullptr;
        }
        const size_t nextObjectCount = objects_.size() + 1u;
        // dirty list は live object 数を超えない。先に容量を確保することで
        // transform/hierarchy 更新後の dirty 登録を非割り当て経路にする。
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
        auto object = std::make_unique<GameObject>(name);
        GameObject* ptr = object.get();
        const size_t storageIndex = objects_.size();
        const RuntimeObjectHandle handle = ReserveObjectHandle();
        if (!handle.IsValid()) {
            return nullptr;
        }
        ptr->SetRuntimeHandle(handle);
        bool objectStored = false;
        try {
            objects_.push_back(std::move(object));
            objectStored = true;
            if (!CommitReservedObjectHandle(
                    handle,
                    ptr,
                    storageIndex)) {
                throw std::logic_error(
                    "World object handle commit rejected.");
            }
            ptr->SetOwnerWorld(this);
        }
        catch (...) {
            RemoveRenderObjectDirty(ptr);
            ptr->SetOwnerWorld(nullptr);
            ptr->SetRuntimeHandle({});
            if (handle.slot < objectSlots_.size() &&
                objectSlots_[handle.slot].state ==
                    ObjectSlotState::Live) {
                ReleaseObjectHandle(handle);
            }
            else {
                ReleaseReservedObjectHandle(handle);
            }
            if (objectStored &&
                !objects_.empty() &&
                objects_.back().get() == ptr) {
                objects_.pop_back();
            }
            throw;
        }
        return ptr;
    }

    World::~World() {
        Clear();
        UnregisterRuntimeIdentity();
    }

    bool World::BindComponentRegistry(
        const ComponentRegistry* registry) noexcept {
        if (registry == nullptr || !IsOwnerThread() || worldClosing_ ||
            structureReadDepth_ != 0u ||
            commandFlushActive_ || commandApplyActive_ ||
            !objects_.empty() || !pendingCommands_.empty() ||
            !executionCommands_.empty()) {
            return false;
        }
        componentRegistry_ = registry;
        return true;
    }

    WorldMutationResult World::DestroyObject(GameObject* object) {
        if (!IsOwnerThread()) {
            return WorldMutationResult::WrongThread;
        }
        if (!CanMutateStructure()) {
            return WorldMutationResult::MutationDisallowed;
        }
        if (object == nullptr || object->ownerWorld_ != this) {
            return WorldMutationResult::InvalidObjectHandle;
        }

        const RuntimeObjectHandle handle =
            object->GetRuntimeHandle();
        if (!handle.IsValid() ||
            handle.slot >= objectSlots_.size()) {
            return WorldMutationResult::InvalidObjectHandle;
        }
        const ObjectSlot& removedSlot =
            objectSlots_[handle.slot];
        if (removedSlot.generation != handle.generation ||
            removedSlot.state != ObjectSlotState::Live ||
            removedSlot.object != object ||
            removedSlot.storageIndex >= objects_.size() ||
            objects_[removedSlot.storageIndex].get() != object) {
            return WorldMutationResult::InternalError;
        }
        const size_t removedStorageIndex =
            removedSlot.storageIndex;
        const size_t lastStorageIndex = objects_.size() - 1u;
        RuntimeObjectHandle movedHandle{};
        if (removedStorageIndex != lastStorageIndex) {
            GameObject* movedObject =
                objects_[lastStorageIndex].get();
            if (movedObject == nullptr) {
                return WorldMutationResult::InternalError;
            }
            movedHandle = movedObject->GetRuntimeHandle();
            if (!movedHandle.IsValid() ||
                movedHandle.slot >= objectSlots_.size()) {
                return WorldMutationResult::InternalError;
            }
            const ObjectSlot& movedSlot =
                objectSlots_[movedHandle.slot];
            if (movedSlot.generation != movedHandle.generation ||
                movedSlot.state != ObjectSlotState::Live ||
                movedSlot.object != movedObject ||
                movedSlot.storageIndex != lastStorageIndex) {
                return WorldMutationResult::InternalError;
            }
        }

        if (removedRenderObjectIds_.size() ==
            removedRenderObjectIds_.max_size()) {
            throw std::length_error(
                "World removed render object capacity overflow.");
        }
        const size_t removedCapacity = INTERNAL::GrowWorldCapacity(
            removedRenderObjectIds_.capacity(),
            removedRenderObjectIds_.size() + 1u,
            removedRenderObjectIds_.max_size());
        removedRenderObjectIds_.reserve(removedCapacity);
        removedRenderObjectIdSet_.reserve(
            removedRenderObjectIdSet_.size() + 1u);
        const size_t removedAdmissionCheckpoint =
            removedRenderObjectIds_.size();
        MarkRenderObjectRemoved(object->GetRenderStableId());
        const StructureReadScope structureRead =
            AcquireStructureReadScope();
        try {
            frameEvents_.Publish(WorldObjectDestroyedEvent{
                object->GetDocumentId(),
                handle,
                object->GetName()
            });
        }
        catch (...) {
            RollbackRemovedRenderObjectAdmission(
                removedAdmissionCheckpoint);
            throw;
        }
        while (!object->children_.empty()) {
            GameObject* child = object->children_.back();
            if (child == nullptr ||
                !child->SetParentInternal(nullptr)) {
                object->children_.pop_back();
            }
        }
        (void)object->SetParentInternal(nullptr);
        (void)OnDocumentIdChanged(
            object,
            object->GetDocumentId(),
            SceneObjectId{});
        object->destroying_ = true;
        object->DetachComponents();
        // The destroying flag rejects owner mutation from OnDetach while
        // World/handle context remains queryable. Remove membership after all
        // callbacks as the final guarantee against a dying queued pointer.
        RemoveRenderObjectDirty(object);
        object->SetRuntimeHandle({});
        object->SetOwnerWorld(nullptr);
        ReleaseObjectHandle(handle);

        if (removedStorageIndex != lastStorageIndex) {
            objects_[removedStorageIndex] =
                std::move(objects_[lastStorageIndex]);
            objectSlots_[movedHandle.slot].storageIndex =
                removedStorageIndex;
        }
        objects_.pop_back();
        return WorldMutationResult::Success;
    }

    WorldMutationResult World::DestroyObject(
        RuntimeObjectHandle handle) {
        if (!IsOwnerThread()) {
            return WorldMutationResult::WrongThread;
        }
        GameObject* object = FindObject(handle);
        if (object == nullptr) {
            return WorldMutationResult::InvalidObjectHandle;
        }
        return DestroyObject(object);
    }

    GameObject* World::FindObject(SceneObjectId id) noexcept {
        if (!IsOwnerThread()) {
            return nullptr;
        }
        if (id.value == 0u) {
            return nullptr;
        }
        const auto found = objectsByDocumentId_.find(id.value);
        return found != objectsByDocumentId_.end()
            ? found->second
            : nullptr;
    }

    const GameObject* World::FindObject(
        SceneObjectId id) const noexcept {
        return const_cast<World*>(this)->FindObject(id);
    }

    void World::Update(float dt) {
        if (!IsOwnerThread()) {
            HIKARI_LOG_ERROR(
                "[World] Update rejected outside the owner thread.");
            return;
        }
        (void)FlushQueuedCommands(
            WorldCommandPhase::ComponentUpdateBegin);
        try {
            const StructureReadScope structureRead =
                AcquireStructureReadScope();
            for (const auto& object : GetObjects()) {
                object->Update(dt);
            }
        }
        catch (...) {
            (void)FlushQueuedCommands(
                WorldCommandPhase::ComponentUpdateEnd);
            throw;
        }
        (void)FlushQueuedCommands(
            WorldCommandPhase::ComponentUpdateEnd);
    }

    void World::Render() {
        if (!IsOwnerThread()) {
            HIKARI_LOG_ERROR(
                "[World] Render rejected outside the owner thread.");
            return;
        }
        const StructureReadScope structureRead =
            AcquireStructureReadScope();
        for (const auto& object : GetObjects()) {
            object->Render();
        }
    }

    void World::RenderImGui() {
        if (!IsOwnerThread()) {
            HIKARI_LOG_ERROR(
                "[World] RenderImGui rejected outside the owner thread.");
            return;
        }
        const StructureReadScope structureRead =
            AcquireStructureReadScope();
        for (const auto& object : GetObjects()) {
            object->RenderImGui();
        }
    }

    void World::Clear() {
        if (!IsOwnerThread()) {
            HIKARI_LOG_ERROR(
                "[World] Clear rejected outside the owner thread.");
            return;
        }
        if (structureReadDepth_ != 0u ||
            commandFlushActive_ ||
            commandApplyActive_ ||
            spawnMaterializationObject_ != nullptr) {
            HIKARI_LOG_ERROR(
                "[World] Clear rejected during structure traversal.");
            return;
        }
        componentRegistry_ = nullptr;
        if (objects_.size() >
            removedRenderObjectIds_.max_size() -
                removedRenderObjectIds_.size()) {
            throw std::length_error(
                "World removed render object capacity overflow.");
        }
        const size_t removedAdmissionCheckpoint =
            removedRenderObjectIds_.size();
        try {
            const size_t removedCapacity =
                INTERNAL::GrowWorldCapacity(
                    removedRenderObjectIds_.capacity(),
                    removedRenderObjectIds_.size() + objects_.size(),
                    removedRenderObjectIds_.max_size());
            removedRenderObjectIds_.reserve(removedCapacity);
            removedRenderObjectIdSet_.reserve(
                removedRenderObjectIdSet_.size() + objects_.size());
            for (const auto& object : GetObjects()) {
                if (object) {
                    MarkRenderObjectRemoved(
                        object->GetRenderStableId());
                }
            }
        }
        catch (...) {
            RollbackRemovedRenderObjectAdmission(
                removedAdmissionCheckpoint);
            throw;
        }
        worldClosing_ = true;
        try {
            for (const PendingWorldCommand& command : pendingCommands_) {
                RecordCommandFailure(
                    command,
                    command.kind == WorldCommandKind::Spawn
                        ? WorldMutationResult::CommandCanceled
                        : WorldMutationResult::WorldClosing,
                    WorldCommandPhase::WorldClear);
            }
            pendingCommands_.clear();
            executionCommands_.clear();
            reservationStateJournal_.clear();
            spawnMaterializationObject_ = nullptr;
            componentCallbackActive_ = false;
            for (uint32_t slotIndex = 0u;
                slotIndex < objectSlots_.size();
                ++slotIndex) {
                const ObjectSlotState state =
                    objectSlots_[slotIndex].state;
                if (state == ObjectSlotState::Reserved ||
                    state == ObjectSlotState::ReservedCanceled) {
                    RecycleObjectSlot(slotIndex);
                }
            }

            const StructureReadScope structureRead =
                AcquireStructureReadScope();
            for (const auto& object : GetObjects()) {
                if (object) {
                    (void)object->SetParentInternal(nullptr);
                }
            }
            for (const auto& object : GetObjects()) {
                if (object) {
                    object->destroying_ = true;
                    object->DetachComponents();
                    object->SetRuntimeHandle({});
                    object->SetOwnerWorld(nullptr);
                }
            }
            while (firstLiveObjectSlot_ !=
                kInvalidLiveObjectSlot) {
                const uint32_t slotIndex =
                    firstLiveObjectSlot_;
                ReleaseObjectHandle(RuntimeObjectHandle{
                    slotIndex,
                    objectSlots_[slotIndex].generation
                });
            }
            objects_.clear();
            objectsByDocumentId_.clear();
            componentBuckets_.clear();
            nativeComponentTypeIds_.clear();
            renderDirtyObjects_.clear();
            frameEvents_.Clear();
            fixedEvents_.Clear();
        }
        catch (...) {
            worldClosing_ = false;
            throw;
        }
        worldClosing_ = false;
    }

    World::LiveObjectRange World::GetObjects() const noexcept {
        return LiveObjectRange(IsOwnerThread() ? this : nullptr);
    }

    const std::vector<GameObject*>&
        World::GetRenderDirtyObjects() const {
        static const std::vector<GameObject*> empty{};
        if (!IsOwnerThread()) {
            return empty;
        }
        return renderDirtyObjects_;
    }

    const std::vector<uint64_t>&
        World::GetRemovedRenderObjectIds() const {
        static const std::vector<uint64_t> empty{};
        if (!IsOwnerThread()) {
            return empty;
        }
        return removedRenderObjectIds_;
    }

    void World::MarkRenderObjectDirty(GameObject* object) {
        if (!IsOwnerThread()) {
            HIKARI_LOG_ERROR(
                "[World] render dirty mutation rejected outside "
                "the owner thread.");
            return;
        }
        if (object == nullptr || object->ownerWorld_ != this) {
            return;
        }
        if (object->renderDirtyIndex_ !=
            GameObject::kInvalidRenderDirtyIndex) {
            if (object->renderDirtyIndex_ >=
                    renderDirtyObjects_.size() ||
                renderDirtyObjects_[object->renderDirtyIndex_] !=
                    object) {
                throw std::logic_error(
                    "World render dirty membership is inconsistent.");
            }
            return;
        }
        const size_t dirtyIndex = renderDirtyObjects_.size();
        renderDirtyObjects_.push_back(object);
        object->renderDirtyIndex_ = dirtyIndex;
    }

    void World::RemoveRenderObjectDirty(
        GameObject* object) noexcept {

        if (object == nullptr ||
            object->renderDirtyIndex_ ==
                GameObject::kInvalidRenderDirtyIndex) {
            return;
        }

        size_t dirtyIndex = object->renderDirtyIndex_;
        if (dirtyIndex >= renderDirtyObjects_.size() ||
            renderDirtyObjects_[dirtyIndex] != object) {
            HIKARI_LOG_ERROR(
                "[World] render dirty membership is inconsistent.");
            dirtyIndex = GameObject::kInvalidRenderDirtyIndex;
            for (size_t index = 0u;
                index < renderDirtyObjects_.size();
                ++index) {
                if (renderDirtyObjects_[index] == object) {
                    dirtyIndex = index;
                    break;
                }
            }
            if (dirtyIndex ==
                GameObject::kInvalidRenderDirtyIndex) {
                object->renderDirtyIndex_ =
                    GameObject::kInvalidRenderDirtyIndex;
                return;
            }
        }

        GameObject* moved = renderDirtyObjects_.back();
        renderDirtyObjects_[dirtyIndex] = moved;
        renderDirtyObjects_.pop_back();
        object->renderDirtyIndex_ =
            GameObject::kInvalidRenderDirtyIndex;
        if (moved != nullptr && moved != object) {
            moved->renderDirtyIndex_ = dirtyIndex;
        }
    }

    void World::MarkRenderObjectRemoved(uint64_t renderObjectId) {
        if (!IsOwnerThread()) {
            HIKARI_LOG_ERROR(
                "[World] render removal rejected outside "
                "the owner thread.");
            return;
        }
        if (renderObjectId == 0u) {
            return;
        }
        const auto [entry, inserted] =
            removedRenderObjectIdSet_.insert(renderObjectId);
        if (!inserted) {
            return;
        }
        try {
            removedRenderObjectIds_.push_back(renderObjectId);
        }
        catch (...) {
            removedRenderObjectIdSet_.erase(entry);
            throw;
        }
    }

    void World::RollbackRemovedRenderObjectAdmission(
        size_t checkpoint) noexcept {

        if (checkpoint >= removedRenderObjectIds_.size()) {
            return;
        }
        for (size_t index = checkpoint;
            index < removedRenderObjectIds_.size();
            ++index) {
            removedRenderObjectIdSet_.erase(
                removedRenderObjectIds_[index]);
        }
        removedRenderObjectIds_.resize(checkpoint);
    }

    void World::AcknowledgeRenderDirtyObjects() {
        if (!IsOwnerThread()) {
            HIKARI_LOG_ERROR(
                "[World] render acknowledgement rejected outside "
                "the owner thread.");
            return;
        }
        for (GameObject* object : renderDirtyObjects_) {
            if (object != nullptr) {
                object->renderDirtyIndex_ =
                    GameObject::kInvalidRenderDirtyIndex;
                object->ClearRenderStateDirty();
            }
        }
        renderDirtyObjects_.clear();
        removedRenderObjectIds_.clear();
        removedRenderObjectIdSet_.clear();
    }

    void World::AcknowledgeAllRenderObjects() {
        // Every World-owned dirty object is admitted exactly once through
        // MarkRenderObjectDirty. Full render-cache rebuilds therefore only
        // need to acknowledge the indexed dirty set, not scan all live objects.
        AcknowledgeRenderDirtyObjects();
    }

    WorldServiceRegistry& World::Services() noexcept {
        return services_;
    }

    const WorldServiceRegistry& World::Services() const noexcept {
        return services_;
    }

    WorldEventStream& World::FrameEvents() noexcept {
        return frameEvents_;
    }

    const WorldEventStream& World::FrameEvents() const noexcept {
        return frameEvents_;
    }

    WorldEventStream& World::FixedEvents() noexcept {
        return fixedEvents_;
    }

    const WorldEventStream& World::FixedEvents() const noexcept {
        return fixedEvents_;
    }

    void World::BeginFrame(uint64_t frameIndex) noexcept {
        frameEvents_.BeginScope(frameIndex);
        commandFailures_.clear();
        commandFailureOverflowCount_ = 0u;
        try {
            (void)FlushQueuedCommands(WorldCommandPhase::BeginFrame);
        }
        catch (...) {
        }
    }

    void World::BeginFixedStep(uint64_t fixedTickIndex) noexcept {
        fixedEvents_.BeginScope(fixedTickIndex);
    }

    bool World::OnDocumentIdChanged(
        GameObject* object,
        SceneObjectId previousId,
        SceneObjectId nextId) {
        if (object == nullptr) {
            return false;
        }

        if (nextId.value != 0u) {
            const auto found = objectsByDocumentId_.find(nextId.value);
            if (found != objectsByDocumentId_.end() &&
                found->second != object) {
                HIKARI_LOG_ERROR(
                    "[World] duplicate SceneObjectId rejected: " +
                    std::to_string(nextId.value));
                return false;
            }
            if (found == objectsByDocumentId_.end()) {
                objectsByDocumentId_.emplace(nextId.value, object);
            }
        }

        if (previousId.value != 0u && previousId != nextId) {
            const auto previous = objectsByDocumentId_.find(
                previousId.value);
            if (previous != objectsByDocumentId_.end() &&
                previous->second == object) {
                objectsByDocumentId_.erase(previous);
            }
        }
        return true;
    }

} // namespace HIKARI
