#include "Scene/HIKARI_World.h"

#include <algorithm>
#include <stdexcept>

#include "Core/HIKARI_Logger.h"
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
        const RuntimeObjectHandle handle = AllocateObjectHandle(ptr);
        if (!handle.IsValid()) {
            return nullptr;
        }
        ptr->SetRuntimeHandle(handle);
        try {
            objects_.push_back(std::move(object));
            ptr->SetOwnerWorld(this);
        }
        catch (...) {
            if (!objects_.empty() && objects_.back().get() == ptr) {
                ptr->SetOwnerWorld(nullptr);
                ptr->SetRuntimeHandle({});
                objects_.pop_back();
            }
            ReleaseObjectHandle(handle);
            throw;
        }
        return ptr;
    }

    World::~World() {
        Clear();
        UnregisterRuntimeIdentity();
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
        const StructureReadScope structureRead =
            AcquireStructureReadScope();
        frameEvents_.Publish(WorldObjectDestroyedEvent{
            object->GetDocumentId(),
            object->GetRuntimeHandle(),
            object->GetName()
        });
        MarkRenderObjectRemoved(object->GetRenderStableId());
        while (!object->children_.empty()) {
            GameObject* child = object->children_.back();
            if (child == nullptr || !child->SetParent(nullptr)) {
                object->children_.pop_back();
            }
        }
        (void)object->SetParent(nullptr);
        renderDirtyObjects_.erase(
            std::remove(
                renderDirtyObjects_.begin(),
                renderDirtyObjects_.end(),
                object),
            renderDirtyObjects_.end());
        (void)OnDocumentIdChanged(
            object,
            object->GetDocumentId(),
            SceneObjectId{});
        object->DetachComponents();
        ReleaseObjectHandle(object->GetRuntimeHandle());
        object->SetRuntimeHandle({});
        object->SetOwnerWorld(nullptr);

        objects_.erase(
            std::remove_if(
                objects_.begin(),
                objects_.end(),
                [object](
                    const std::unique_ptr<GameObject>& candidate) {
                    return candidate.get() == object;
                }),
            objects_.end());
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
            for (const auto& object : objects_) {
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
        for (const auto& object : objects_) {
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
        for (const auto& object : objects_) {
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
            commandApplyActive_) {
            HIKARI_LOG_ERROR(
                "[World] Clear rejected during structure traversal.");
            return;
        }
        if (objects_.size() >
            removedRenderObjectIds_.max_size() -
                removedRenderObjectIds_.size()) {
            throw std::length_error(
                "World removed render object capacity overflow.");
        }
        const size_t removedCapacity = INTERNAL::GrowWorldCapacity(
            removedRenderObjectIds_.capacity(),
            removedRenderObjectIds_.size() + objects_.size(),
            removedRenderObjectIds_.max_size());
        removedRenderObjectIds_.reserve(removedCapacity);
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
            for (const auto& object : objects_) {
                if (object) {
                    (void)object->SetParent(nullptr);
                }
            }
            for (const auto& object : objects_) {
                if (object) {
                    MarkRenderObjectRemoved(
                        object->GetRenderStableId());
                    object->DetachComponents();
                    ReleaseObjectHandle(object->GetRuntimeHandle());
                    object->SetRuntimeHandle({});
                    object->SetOwnerWorld(nullptr);
                }
            }
            objects_.clear();
            objectsByDocumentId_.clear();
            componentBuckets_.clear();
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

    const std::vector<std::unique_ptr<GameObject>>&
        World::GetObjects() const {
        return objects_;
    }

    const std::vector<GameObject*>&
        World::GetRenderDirtyObjects() const {
        return renderDirtyObjects_;
    }

    const std::vector<uint64_t>&
        World::GetRemovedRenderObjectIds() const {
        return removedRenderObjectIds_;
    }

    void World::MarkRenderObjectDirty(GameObject* object) {
        if (object == nullptr) {
            return;
        }
        if (std::find(
                renderDirtyObjects_.begin(),
                renderDirtyObjects_.end(),
                object) != renderDirtyObjects_.end()) {
            return;
        }
        renderDirtyObjects_.push_back(object);
    }

    void World::MarkRenderObjectRemoved(uint64_t renderObjectId) {
        if (renderObjectId == 0u) {
            return;
        }
        if (std::find(
                removedRenderObjectIds_.begin(),
                removedRenderObjectIds_.end(),
                renderObjectId) != removedRenderObjectIds_.end()) {
            return;
        }
        removedRenderObjectIds_.push_back(renderObjectId);
    }

    void World::AcknowledgeRenderDirtyObjects() {
        for (GameObject* object : renderDirtyObjects_) {
            if (object != nullptr) {
                object->ClearRenderStateDirty();
            }
        }
        renderDirtyObjects_.clear();
        removedRenderObjectIds_.clear();
    }

    void World::AcknowledgeAllRenderObjects() {
        for (const auto& object : objects_) {
            if (object) {
                object->ClearRenderStateDirty();
            }
        }
        renderDirtyObjects_.clear();
        removedRenderObjectIds_.clear();
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
