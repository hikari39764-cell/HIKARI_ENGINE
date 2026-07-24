#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "HIKARI_GameObject.h"
#include "Scene/HIKARI_WorldEventStream.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"
namespace HIKARI {


    class World {
    public:
        World() = default;
        ~World();

        GameObject* CreateObject(const std::string& name);
        void DestroyObject(GameObject* object);
        GameObject* FindObject(RuntimeObjectHandle handle) noexcept;
        const GameObject* FindObject(RuntimeObjectHandle handle) const noexcept;
        GameObject* FindObject(SceneObjectId id) noexcept;
        const GameObject* FindObject(SceneObjectId id) const noexcept;

        void Update(float dt);
        void Render();
        void RenderImGui();
        void Clear();

        const std::vector<std::unique_ptr<GameObject>>& GetObjects() const;
        const std::vector<GameObject*>& GetRenderDirtyObjects() const;
        const std::vector<uint64_t>& GetRemovedRenderObjectIds() const;
        void MarkRenderObjectDirty(GameObject* object);
        void MarkRenderObjectRemoved(uint64_t renderObjectId);
        void AcknowledgeRenderDirtyObjects();
        void AcknowledgeAllRenderObjects();

        WorldServiceRegistry& Services() noexcept;
        const WorldServiceRegistry& Services() const noexcept;
        WorldEventStream& FrameEvents() noexcept;
        const WorldEventStream& FrameEvents() const noexcept;
        WorldEventStream& FixedEvents() noexcept;
        const WorldEventStream& FixedEvents() const noexcept;
        void BeginFrame(uint64_t frameIndex) noexcept;
        void BeginFixedStep(uint64_t fixedTickIndex) noexcept;

        template<class T, class Fn>
        void ForEachObjectWith(Fn&& fn) {
            for (const auto& object : objects_) {
                if (auto* component = object->GetComponent<T>()) {
                    std::forward<Fn>(fn)(*object, *component);
                }
            }
        }

        template<class T1, class T2, class Fn>
        void ForEachObjectWith(Fn&& fn) {
            for (const auto& object : objects_) {
                auto* c1 = object->GetComponent<T1>();
                auto* c2 = object->GetComponent<T2>();
                if (c1 && c2) {
                    std::forward<Fn>(fn)(*object, *c1, *c2);
                }
            }
        }

    private:
        friend class GameObject;

        struct ObjectSlot {
            GameObject* object = nullptr;
            uint32_t generation = 1;
        };

        RuntimeObjectHandle AllocateObjectHandle(GameObject* object);
        void ReleaseObjectHandle(RuntimeObjectHandle handle);
        void OnDocumentIdChanged(
            GameObject* object,
            SceneObjectId previousId,
            SceneObjectId nextId);

        std::vector<std::unique_ptr<GameObject>> objects_;
        std::vector<ObjectSlot> objectSlots_{};
        std::vector<uint32_t> freeObjectSlots_{};
        std::unordered_map<uint64_t, GameObject*> objectsByDocumentId_{};
        std::vector<GameObject*> renderDirtyObjects_{};
        std::vector<uint64_t> removedRenderObjectIds_{};
        WorldServiceRegistry services_{};
        WorldEventStream frameEvents_{};
        WorldEventStream fixedEvents_{};
    };

} // namespace HIKARI
