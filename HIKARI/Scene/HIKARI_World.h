#pragma once
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
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
                if (world_ != nullptr) {
                    --world_->structureReadDepth_;
                }
            }

            bool IsValid() const noexcept {
                return world_ != nullptr;
            }

        private:
            friend class World;

            explicit StructureReadScope(World& world) noexcept
                : world_(world.IsOwnerThread() ? &world : nullptr) {
                if (world_ != nullptr) {
                    ++world_->structureReadDepth_;
                }
            }

            World* world_ = nullptr;
        };

        // Owner-thread transient view in deterministic attachment order.
        // Iterators must not be retained across any structural mutation.
        class LiveObjectRange {
        public:
            class Iterator {
            public:
                const std::unique_ptr<GameObject>&
                    operator*() const noexcept;
                Iterator& operator++() noexcept;
                bool operator==(
                    const Iterator& other) const noexcept;
                bool operator!=(
                    const Iterator& other) const noexcept;

            private:
                friend class LiveObjectRange;

                Iterator(
                    const World* world,
                    uint32_t slot) noexcept
                    : world_(world), slot_(slot) {}

                const World* world_ = nullptr;
                uint32_t slot_ = 0u;
            };

            Iterator begin() const noexcept;
            Iterator end() const noexcept;
            size_t size() const noexcept;
            bool empty() const noexcept;

        private:
            friend class World;

            explicit LiveObjectRange(
                const World* world) noexcept
                : world_(world) {}

            const World* world_ = nullptr;
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

        LiveObjectRange GetObjects() const noexcept;
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
            static_assert(std::is_base_of_v<IComponent, T>);
            VisitIndexedComponentType<T>(std::forward<Fn>(fn));
        }

        template<class T1, class T2, class Fn>
        void ForEachObjectWith(Fn&& fn) {
            static_assert(std::is_base_of_v<IComponent, T1>);
            static_assert(std::is_base_of_v<IComponent, T2>);
            // T1 is the deterministic attachment-order anchor. Put the more
            // selective component first at call sites that need a sparse join.
            auto join = [&](GameObject& object, T1& first) {
                if (T2* second = object.GetComponent<T2>()) {
                    fn(object, first, *second);
                }
            };
            VisitIndexedComponentType<T1>(join);
        }

    private:
        friend class GameObject;
        friend struct GameProjectSdkAccess;

        using IndexedComponentVisitorFn = bool (*)(
            void* context,
            RuntimeObjectHandle object,
            IComponent* component) noexcept;

        template<class T, class Fn>
        void VisitIndexedComponentType(Fn&& fn) {
            if (!IsOwnerThread()) {
                return;
            }
            const ComponentTypeId typeId =
                ResolveIndexedComponentTypeId<T>();
            if (!typeId.IsValid()) {
                return;
            }

            using Callback = std::remove_reference_t<Fn>;
            struct VisitContext {
                World* world = nullptr;
                Callback* callback = nullptr;
                std::exception_ptr exception{};
            };
            VisitContext context{ this, std::addressof(fn), {} };
            (void)VisitIndexedComponents(
                typeId,
                &context,
                [](void* rawContext,
                    RuntimeObjectHandle objectHandle,
                    IComponent* component) noexcept {
                    VisitContext& visit =
                        *static_cast<VisitContext*>(rawContext);
                    try {
                        GameObject* object =
                            visit.world->FindObject(objectHandle);
                        T* typed = dynamic_cast<T*>(component);
                        if (object == nullptr || typed == nullptr) {
                            return false;
                        }
                        (*visit.callback)(*object, *typed);
                        return true;
                    }
                    catch (...) {
                        visit.exception = std::current_exception();
                        return false;
                    }
                });
            if (context.exception) {
                std::rethrow_exception(context.exception);
            }
        }

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
        void RemoveRenderObjectDirty(
            GameObject* object) noexcept;
        void RollbackRemovedRenderObjectAdmission(
            size_t checkpoint) noexcept;
        bool VisitIndexedComponents(
            ComponentTypeId typeId,
            void* context,
            IndexedComponentVisitorFn visitor) noexcept;

        template<class T>
        ComponentTypeId ResolveIndexedComponentTypeId() const noexcept {
            ComponentTypeId resolved{};
            for (const auto& [_, bucket] : componentBuckets_) {
                if (bucket.components.empty() ||
                    bucket.head == kInvalidWorldComponentIndex ||
                    bucket.head >= bucket.components.size()) {
                    continue;
                }
                IComponent* component =
                    bucket.components[bucket.head].component;
                if (component == nullptr ||
                    dynamic_cast<T*>(component) == nullptr) {
                    continue;
                }
                if (resolved.IsValid() && resolved != bucket.typeId) {
                    // One C++ adapter type may project several declared game
                    // component ids. Such ambiguous types require the public
                    // explicit-id query and cannot use this internal helper.
                    return {};
                }
                resolved = bucket.typeId;
            }
            return resolved;
        }

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

        static constexpr uint32_t kInvalidLiveObjectSlot =
            (std::numeric_limits<uint32_t>::max)();
        static constexpr size_t kInvalidObjectStorageIndex =
            (std::numeric_limits<size_t>::max)();

        struct ObjectSlot {
            GameObject* object = nullptr;
            size_t storageIndex = kInvalidObjectStorageIndex;
            uint32_t previousLiveSlot = kInvalidLiveObjectSlot;
            uint32_t nextLiveSlot = kInvalidLiveObjectSlot;
            uint32_t generation = 1u;
            uint32_t reservationToken = 0u;
            ObjectSlotState state = ObjectSlotState::Free;
        };

        struct ReservedStateJournalEntry {
            RuntimeObjectHandle object{};
            ObjectSlotState previousState = ObjectSlotState::Reserved;
        };

        RuntimeObjectHandle ReserveObjectHandle();
        bool CommitReservedObjectHandle(
            RuntimeObjectHandle handle,
            GameObject* object,
            size_t storageIndex) noexcept;
        void ReleaseReservedObjectHandle(
            RuntimeObjectHandle handle) noexcept;
        bool IsReservedObjectHandle(
            RuntimeObjectHandle handle) const noexcept;
        bool IsKnownObjectHandle(
            RuntimeObjectHandle handle) const noexcept;
        void RecycleObjectSlot(uint32_t slotIndex) noexcept;
        void AppendLiveObjectSlot(uint32_t slotIndex) noexcept;
        void RemoveLiveObjectSlot(uint32_t slotIndex) noexcept;
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
        uint32_t firstLiveObjectSlot_ = kInvalidLiveObjectSlot;
        uint32_t lastLiveObjectSlot_ = kInvalidLiveObjectSlot;
        size_t liveObjectCount_ = 0u;
        std::unordered_map<uint64_t, GameObject*> objectsByDocumentId_{};
        std::unordered_map<uint64_t, WorldComponentBucket>
            componentBuckets_{};
        std::vector<GameObject*> renderDirtyObjects_{};
        std::vector<uint64_t> removedRenderObjectIds_{};
        std::unordered_set<uint64_t>
            removedRenderObjectIdSet_{};
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
