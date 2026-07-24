#include "HIKARI_World.h"
#include <algorithm>
#include <limits>
#include "Core/HIKARI_Logger.h"
#include "HIKARI_GameObject.h"
#include "Scene/HIKARI_WorldEvents.h"
namespace HIKARI {



    GameObject* World::CreateObject(const std::string& name) {
        auto object = std::make_unique<GameObject>(name);
        GameObject* ptr = object.get();
        ptr->SetRuntimeHandle(AllocateObjectHandle(ptr));
        ptr->SetOwnerWorld(this);
        objects_.push_back(std::move(object));
        MarkRenderObjectDirty(ptr);
        return ptr;
    }

    World::~World() = default;

    void World::DestroyObject(GameObject* object) {
        if (object == nullptr || object->ownerWorld_ != this) {
            return;
        }

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
            std::remove(renderDirtyObjects_.begin(), renderDirtyObjects_.end(), object),
            renderDirtyObjects_.end());
        OnDocumentIdChanged(
            object,
            object->GetDocumentId(),
            SceneObjectId{});
        ReleaseObjectHandle(object->GetRuntimeHandle());
        object->SetRuntimeHandle({});
        object->SetOwnerWorld(nullptr);

        objects_.erase(
            std::remove_if(objects_.begin(), objects_.end(), [object](const std::unique_ptr<GameObject>& candidate) {
                return candidate.get() == object;
                }),
            objects_.end());
    }

    GameObject* World::FindObject(RuntimeObjectHandle handle) noexcept {
        if (!handle.IsValid() || handle.slot >= objectSlots_.size()) {
            return nullptr;
        }
        const ObjectSlot& slot = objectSlots_[handle.slot];
        return slot.generation == handle.generation
            ? slot.object
            : nullptr;
    }

    const GameObject* World::FindObject(
        RuntimeObjectHandle handle) const noexcept {

        return const_cast<World*>(this)->FindObject(handle);
    }

    GameObject* World::FindObject(SceneObjectId id) noexcept {
        if (id.value == 0) {
            return nullptr;
        }
        const auto found = objectsByDocumentId_.find(id.value);
        return found != objectsByDocumentId_.end()
            ? found->second
            : nullptr;
    }

    const GameObject* World::FindObject(SceneObjectId id) const noexcept {
        return const_cast<World*>(this)->FindObject(id);
    }

    void World::Update(float dt) {
        for (const auto& object : objects_) {
            object->Update(dt);
        }
    }

    void World::Render() {
        for (const auto& object : objects_) {
            object->Render();
        }
    }

    void World::RenderImGui() {
        for (const auto& object : objects_) {
            object->RenderImGui();
        }
    }

    void World::Clear() {
        for (const auto& object : objects_) {
            if (object) {
                (void)object->SetParent(nullptr);
            }
        }
        for (const auto& object : objects_) {
            if (object) {
                MarkRenderObjectRemoved(object->GetRenderStableId());
                ReleaseObjectHandle(object->GetRuntimeHandle());
                object->SetRuntimeHandle({});
                object->SetOwnerWorld(nullptr);
            }
        }
        objects_.clear();
        objectsByDocumentId_.clear();
        renderDirtyObjects_.clear();
        frameEvents_.Clear();
        fixedEvents_.Clear();
    }

    const std::vector<std::unique_ptr<GameObject>>& World::GetObjects() const {
        return objects_;
    }

    const std::vector<GameObject*>& World::GetRenderDirtyObjects() const {
        return renderDirtyObjects_;
    }

    const std::vector<uint64_t>& World::GetRemovedRenderObjectIds() const {
        return removedRenderObjectIds_;
    }

    void World::MarkRenderObjectDirty(GameObject* object) {
        if (object == nullptr) {
            return;
        }
        if (std::find(renderDirtyObjects_.begin(), renderDirtyObjects_.end(), object) !=
            renderDirtyObjects_.end()) {
            return;
        }
        renderDirtyObjects_.push_back(object);
    }

    void World::MarkRenderObjectRemoved(uint64_t renderObjectId) {
        if (renderObjectId == 0) {
            return;
        }
        if (std::find(removedRenderObjectIds_.begin(), removedRenderObjectIds_.end(), renderObjectId) !=
            removedRenderObjectIds_.end()) {
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
    }

    void World::BeginFixedStep(uint64_t fixedTickIndex) noexcept {
        fixedEvents_.BeginScope(fixedTickIndex);
    }

    RuntimeObjectHandle World::AllocateObjectHandle(GameObject* object) {
        if (!freeObjectSlots_.empty()) {
            const uint32_t slotIndex = freeObjectSlots_.back();
            freeObjectSlots_.pop_back();
            ObjectSlot& slot = objectSlots_[slotIndex];
            slot.object = object;
            if (slot.generation == 0) {
                slot.generation = 1;
            }
            return RuntimeObjectHandle{ slotIndex, slot.generation };
        }

        if (objectSlots_.size() >=
            static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
            return {};
        }
        const uint32_t slotIndex =
            static_cast<uint32_t>(objectSlots_.size());
        objectSlots_.push_back(ObjectSlot{ object, 1 });
        return RuntimeObjectHandle{ slotIndex, 1 };
    }

    void World::ReleaseObjectHandle(RuntimeObjectHandle handle) {
        if (!handle.IsValid() || handle.slot >= objectSlots_.size()) {
            return;
        }
        ObjectSlot& slot = objectSlots_[handle.slot];
        if (slot.generation != handle.generation || slot.object == nullptr) {
            return;
        }
        slot.object = nullptr;
        ++slot.generation;
        if (slot.generation == 0) {
            slot.generation = 1;
        }
        freeObjectSlots_.push_back(handle.slot);
    }

    void World::OnDocumentIdChanged(
        GameObject* object,
        SceneObjectId previousId,
        SceneObjectId nextId) {

        if (previousId.value != 0) {
            const auto previous = objectsByDocumentId_.find(
                previousId.value);
            if (previous != objectsByDocumentId_.end() &&
                previous->second == object) {
                objectsByDocumentId_.erase(previous);
            }
        }
        if (object == nullptr || nextId.value == 0) {
            return;
        }

        const auto [found, inserted] =
            objectsByDocumentId_.emplace(nextId.value, object);
        if (!inserted && found->second != object) {
            HIKARI_LOG_ERROR(
                "[World] duplicate SceneObjectId: " +
                std::to_string(nextId.value));
        }
    }

} // namespace HIKARI
