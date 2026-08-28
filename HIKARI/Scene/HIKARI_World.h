#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include "HIKARI_GameObject.h"
#include "Scene/HIKARI_WorldCommands.h"
#include "Scene/HIKARI_WorldComponentIndex.h"
#include "Scene/HIKARI_WorldEventStream.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"
namespace HIKARI {

    struct GameProjectSdkAccess;

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
        WorldCommandSubmission SubmitSpawn(
            std::string_view name,
            const Transform3D& localTransform,
            RuntimeObjectHandle parent = {});
        WorldMutationResult SubmitSpawnBatch(
            std::span<const WorldSpawnRequest> requests,
            std::span<WorldCommandSubmission> outSubmissions);
        WorldCommandSubmission SubmitDestroy(
            RuntimeObjectHandle object);
        WorldCommandSubmission SubmitSetParent(
            RuntimeObjectHandle object,
            RuntimeObjectHandle parent,
            WorldParentMode mode);
        uint32_t FlushQueuedCommands(WorldCommandPhase phase);
        uint32_t GetCommandFailureCount() const noexcept;
        bool ReadCommandFailure(
            uint32_t index,
            WorldCommandFailure& outFailure) const noexcept;
        uint32_t GetCommandFailureOverflowCount() const noexcept;
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
            return IsOwnerThread() &&
                structureReadDepth_ == 0u &&
                !worldClosing_ &&
                ((!commandFlushActive_ && pendingCommands_.empty()) ||
                 commandApplyActive_);
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
        friend struct GameProjectSdkAccess;

        using IndexedComponentVisitorFn = bool (*)(
            void* context,
            RuntimeObjectHandle object,
            IComponent* component) noexcept;

        bool CanEnterStructureCallback() const noexcept {
            return IsOwnerThread() &&
                structureReadDepth_ == 0u &&
                !commandFlushActive_ &&
                !worldClosing_;
        }

        bool PrepareComponentAttachment(
            IComponent& component,
            ComponentTypeId declaredTypeId);
        void CancelComponentAttachment(
            IComponent& component) noexcept;
        bool CommitComponentAttachment(
            GameObject& object,
            IComponent& component) noexcept;
        void RemoveComponentFromIndex(
            IComponent& component) noexcept;
        bool VisitIndexedComponents(
            ComponentTypeId typeId,
            void* context,
            IndexedComponentVisitorFn visitor) noexcept;

        struct WorldCommandCheckpoint {
            size_t pendingCommandCount = 0u;
            size_t reservationJournalCount = 0u;
        };

        WorldCommandCheckpoint BeginComponentCallback();
        void CommitComponentCallback(
            WorldCommandCheckpoint checkpoint) noexcept;
        void RollbackComponentCallback(
            WorldCommandCheckpoint checkpoint) noexcept;

        enum class ObjectSlotState : uint8_t {
            Free,
            Reserved,
            ReservedCanceled,
            Live,
            Retired
        };

        struct ObjectSlot {
            GameObject* object = nullptr;
            uint32_t generation = 1;
            uint32_t reservationToken = 0u;
            ObjectSlotState state = ObjectSlotState::Free;
        };

        struct ReservedStateJournalEntry {
            RuntimeObjectHandle object{};
            ObjectSlotState previousState = ObjectSlotState::Reserved;
        };

        RuntimeObjectHandle AllocateObjectHandle(GameObject* object);
        RuntimeObjectHandle ReserveObjectHandle();
        bool CommitReservedObjectHandle(
            RuntimeObjectHandle handle,
            GameObject* object) noexcept;
        void ReleaseReservedObjectHandle(
            RuntimeObjectHandle handle) noexcept;
        bool IsReservedObjectHandle(
            RuntimeObjectHandle handle) const noexcept;
        bool IsKnownObjectHandle(
            RuntimeObjectHandle handle) const noexcept;
        void RecycleObjectSlot(uint32_t slotIndex) noexcept;
        void ReleaseObjectHandle(RuntimeObjectHandle handle);
        WorldCommandSubmission QueueCommand(PendingWorldCommand command);
        uint32_t AllocateCommandToken() noexcept;
        void RecordCommandFailure(
            const PendingWorldCommand& command,
            WorldMutationResult result,
            WorldCommandPhase phase) noexcept;
        WorldMutationResult ApplyQueuedSpawn(
            const PendingWorldCommand& command);
        void UnregisterRuntimeIdentity() noexcept;
        bool OnDocumentIdChanged(
            GameObject* object,
            SceneObjectId previousId,
            SceneObjectId nextId);

        std::vector<std::unique_ptr<GameObject>> objects_;
        std::vector<ObjectSlot> objectSlots_{};
        std::vector<uint32_t> freeObjectSlots_{};
        std::unordered_map<uint64_t, GameObject*> objectsByDocumentId_{};
        std::unordered_map<uint64_t, WorldComponentBucket>
            componentBuckets_{};
        std::vector<GameObject*> renderDirtyObjects_{};
        std::vector<uint64_t> removedRenderObjectIds_{};
        WorldServiceRegistry services_{};
        WorldEventStream frameEvents_{};
        WorldEventStream fixedEvents_{};
        std::vector<PendingWorldCommand> pendingCommands_{};
        std::vector<PendingWorldCommand> executionCommands_{};
        std::vector<WorldCommandFailure> commandFailures_{};
        std::vector<ReservedStateJournalEntry>
            reservationStateJournal_{};
        std::thread::id ownerThread_{};
        uint64_t runtimeIdentity_ = 0u;
        uint64_t nextCommandSequence_ = 1u;
        uint32_t nextCommandToken_ = 1u;
        uint32_t commandFailureOverflowCount_ = 0u;
        uint32_t structureReadDepth_ = 0u;
        bool commandFlushActive_ = false;
        bool commandApplyActive_ = false;
        bool componentCallbackActive_ = false;
        bool worldClosing_ = false;
    };

} // namespace HIKARI
