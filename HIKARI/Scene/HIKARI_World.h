#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include "HIKARI_GameObject.h"
#include "Scene/HIKARI_WorldEventStream.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"
namespace HIKARI {

    enum class WorldMutationResult : uint8_t {
        Success,
        WrongThread,
        MutationDisallowed,
        InvalidObjectHandle,
        InvalidParentHandle,
        ParentCycle,
        InvalidParentMode,
        TransformNotInvertible,
        TransformDecompositionFailed,
        HierarchyRejected
    };

    enum class WorldParentMode : uint8_t {
        KeepLocal,
        KeepWorld
    };

    enum class RuntimeWorldAccessResult : uint8_t {
        Success,
        InvalidWorld,
        WrongThread
    };

    class World {
    public:
        class StructureReadScope {
        public:
            StructureReadScope(const StructureReadScope&) = delete;
            StructureReadScope& operator=(const StructureReadScope&) = delete;

            ~StructureReadScope() noexcept {
                --world_->structureReadDepth_;
            }

        private:
            friend class World;

            explicit StructureReadScope(World& world) noexcept
                : world_(&world) {
                ++world_->structureReadDepth_;
            }

            World* world_ = nullptr;
        };

        World();
        ~World();

        GameObject* CreateObject(const std::string& name);
        WorldMutationResult DestroyObject(GameObject* object);
        WorldMutationResult DestroyObject(RuntimeObjectHandle handle);
        // 無効な parent handle はルートへの付け替えを表す。
        WorldMutationResult SetParent(
            RuntimeObjectHandle object,
            RuntimeObjectHandle parent = {},
            WorldParentMode mode = WorldParentMode::KeepLocal);
        WorldMutationResult SetWorldTransform(
            RuntimeObjectHandle object,
            const Transform3D& transform);
        GameObject* FindObject(RuntimeObjectHandle handle) noexcept;
        const GameObject* FindObject(RuntimeObjectHandle handle) const noexcept;
        bool TryGetLocalTransform(
            RuntimeObjectHandle object,
            Transform3D& outTransform) const noexcept;
        bool TryGetWorldTransform(
            RuntimeObjectHandle object,
            Transform3D& outTransform) const noexcept;
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
        uint64_t GetRuntimeIdentity() const noexcept {
            return runtimeIdentity_;
        }
        bool IsOwnerThread() const noexcept {
            return std::this_thread::get_id() == ownerThread_;
        }
        static World* ResolveRuntimeWorld(
            uint64_t identity,
            RuntimeWorldAccessResult* outAccess = nullptr) noexcept;
        bool CanMutateStructure() const noexcept {
            return IsOwnerThread() && structureReadDepth_ == 0u;
        }
        [[nodiscard]] StructureReadScope AcquireStructureReadScope() noexcept {
            return StructureReadScope(*this);
        }

        template<class T, class Fn>
        void ForEachObjectWith(Fn&& fn) {
            if (!IsOwnerThread()) {
                return;
            }
            const StructureReadScope structureRead =
                AcquireStructureReadScope();
            for (const auto& object : objects_) {
                if (auto* component = object->GetComponent<T>()) {
                    std::forward<Fn>(fn)(*object, *component);
                }
            }
        }

        template<class T1, class T2, class Fn>
        void ForEachObjectWith(Fn&& fn) {
            if (!IsOwnerThread()) {
                return;
            }
            const StructureReadScope structureRead =
                AcquireStructureReadScope();
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
        void UnregisterRuntimeIdentity() noexcept;
        bool OnDocumentIdChanged(
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
        std::thread::id ownerThread_{};
        uint64_t runtimeIdentity_ = 0u;
        uint32_t structureReadDepth_ = 0u;
    };

} // namespace HIKARI
