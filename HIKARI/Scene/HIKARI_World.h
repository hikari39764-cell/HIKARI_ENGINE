#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "HIKARI/Project/PrefabTypes.h"
#include "HIKARI_GameObject.h"
#include "Scene/HIKARI_WorldCommands.h"
#include "Scene/HIKARI_WorldComponentIndex.h"
#include "Scene/HIKARI_WorldEventStream.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"
namespace HIKARI {

class ComponentRegistry;
class WorldInstanceStore;
class PrefabView;
class SystemScheduler;
struct GameProjectSdkAccess;

enum class RuntimeWorldAccessResult : uint8_t { Success, InvalidWorld, WrongThread };

class INavigationGeometrySource;
class IRenderSurfaceExtension;
class IWaterSource;

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

        bool IsValid() const noexcept { return world_ != nullptr; }

    private:
        friend class World;

        explicit StructureReadScope(World& world) noexcept : world_(world.IsOwnerThread() ? &world : nullptr) {
            if (world_ != nullptr) {
                ++world_->structureReadDepth_;
            }
        }

        World* world_ = nullptr;
    };

    // GameObject-backed owner-thread transient view in deterministic
    // attachment order. World mass rows use VisitMassObjects instead.
    // Iterators must not be retained across any structural mutation.
    class LiveObjectRange {
    public:
        class Iterator {
        public:
            const std::unique_ptr<GameObject>& operator*() const noexcept;
            Iterator& operator++() noexcept;
            bool operator==(const Iterator& other) const noexcept;
            bool operator!=(const Iterator& other) const noexcept;

        private:
            friend class LiveObjectRange;

            Iterator(const World* world, uint32_t slot) noexcept : world_(world), slot_(slot) {}

            const World* world_ = nullptr;
            uint32_t slot_ = 0u;
        };

        Iterator begin() const noexcept;
        Iterator end() const noexcept;
        size_t size() const noexcept;
        bool empty() const noexcept;

    private:
        friend class World;

        explicit LiveObjectRange(const World* world) noexcept : world_(world) {}

        const World* world_ = nullptr;
    };

    World();
    ~World();

    GameObject* CreateObject(const std::string& name);
    WorldMutationResult DestroyObject(GameObject* object);
    WorldMutationResult DestroyObject(RuntimeObjectHandle handle);
    // 無効な parent handle はルートへの付け替えを表す。
    WorldMutationResult SetParent(RuntimeObjectHandle object, RuntimeObjectHandle parent = {},
                                  WorldParentMode mode = WorldParentMode::KeepLocal);
    WorldCommandSubmission SubmitSpawn(std::string_view name, const Transform3D& localTransform,
                                       RuntimeObjectHandle parent = {});
    WorldMutationResult SubmitSpawnBatch(std::span<const WorldSpawnRequest> requests,
                                         std::span<WorldCommandSubmission> outSubmissions);
    WorldMutationResult SubmitMassSpawnBatch(const WorldMassSpawnBatchRequest& request,
                                             std::span<WorldCommandSubmission> outSubmissions);
    WorldMutationResult SubmitMassDestroyBatch(std::span<const RuntimeObjectHandle> objects,
                                               std::span<WorldCommandSubmission> outSubmissions);
    // Borrowed composition-root registry. Bind only while the World is
    // empty; Clear retains it through component detach and releases it
    // after object and component-index teardown completes.
    bool BindComponentRegistry(const ComponentRegistry* registry) noexcept;
    const ComponentRegistry* GetComponentRegistry() const noexcept { return componentRegistry_; }
    bool BindMassSchemaRegistry(const WorldMassSchemaRegistry* registry) noexcept;
    WorldCommandSubmission SubmitDestroy(RuntimeObjectHandle object);
    WorldCommandSubmission SubmitSetParent(RuntimeObjectHandle object, RuntimeObjectHandle parent,
                                           WorldParentMode mode);
    uint32_t FlushQueuedCommands(WorldCommandPhase phase);
    uint32_t GetCommandFailureCount() const noexcept;
    bool ReadCommandFailure(uint32_t index, WorldCommandFailure& outFailure) const noexcept;
    uint32_t GetCommandFailureOverflowCount() const noexcept;
    WorldMutationResult SetLocalTransform(RuntimeObjectHandle object, const Transform3D& transform);
    WorldMutationResult SetWorldTransform(RuntimeObjectHandle object, const Transform3D& transform);
    GameObject* FindObject(RuntimeObjectHandle handle) noexcept;
    const GameObject* FindObject(RuntimeObjectHandle handle) const noexcept;
    IComponent* FindFirstIndexedComponent(ComponentTypeId typeId,
                                          RuntimeObjectHandle object) noexcept; // Borrow until structure changes.
    bool TryGetLocalTransform(RuntimeObjectHandle object, Transform3D& outTransform) const noexcept;
    bool TryGetWorldTransform(RuntimeObjectHandle object, Transform3D& outTransform) const noexcept;
    size_t GetMassObjectCount(ComponentTypeId typeId) const noexcept;
    const WorldMassSchema* FindMassSchema(ComponentTypeId typeId) const noexcept;
    size_t GetTotalObjectCount() const noexcept;
    GameObject* FindObject(SceneObjectId id) noexcept;
    const GameObject* FindObject(SceneObjectId id) const noexcept;

    void Update(float dt);
    void Render();
    void Clear();

    LiveObjectRange GetObjects() const noexcept;
    const std::vector<GameObject*>& GetRenderDirtyObjects() const;
    const std::vector<uint64_t>& GetRemovedRenderObjectIds() const;
    void MarkRenderObjectDirty(GameObject* object);
    void MarkRenderObjectRemoved(uint64_t renderObjectId);
    void AcknowledgeRenderDirtyObjects();
    void AcknowledgeAllRenderObjects();
    bool PublishMassRenderProxies(uint64_t frameIndex, void* context,
                                  WorldMassStore::RenderPublicationVisitorFn visitor,
                                  WorldMassRenderPublicationReceipt& outReceipt) noexcept;
    bool AcknowledgeMassRenderProxies(const WorldMassRenderPublicationReceipt& receipt) noexcept;
    // 場景の実例集（場景の文書が持つ。世界より長生きする）。描画の発布に含め、SDK からも読み書きできる。
    void BindInstanceStore(WorldInstanceStore* store) noexcept { instanceStore_ = store; }
    WorldInstanceStore* GetInstanceStore() const noexcept { return instanceStore_; }

    WorldServiceRegistry& Services() noexcept;
    const WorldServiceRegistry& Services() const noexcept;
    WorldEventStream& FrameEvents() noexcept;
    const WorldEventStream& FrameEvents() const noexcept;
    WorldEventStream& FixedEvents() noexcept;
    const WorldEventStream& FixedEvents() const noexcept;
    void BeginFrame(uint64_t frameIndex) noexcept;
    void BeginFixedStep(uint64_t fixedTickIndex) noexcept;
    uint64_t GetRuntimeIdentity() const noexcept { return runtimeIdentity_; }
    // モジュールの面。世界は持ち主にならず、登録した側が外す。
    std::span<IRenderSurfaceExtension* const> GetRenderSurfaceExtensions() const noexcept {
        return renderSurfaceExtensions_;
    }
    void AddRenderSurfaceExtension(IRenderSurfaceExtension& extension);
    std::span<INavigationGeometrySource* const> GetNavigationGeometrySources() const noexcept {
        return navigationGeometrySources_;
    }
    void AddNavigationGeometrySource(INavigationGeometrySource& source);
    void RemoveNavigationGeometrySource(INavigationGeometrySource& source) noexcept;
    // モジュールの水。世界は持ち主にならず、登録した側が外す。
    std::span<IWaterSource* const> GetWaterSources() const noexcept { return waterSources_; }
    void AddWaterSource(IWaterSource& source);
    void RemoveWaterSource(IWaterSource& source) noexcept;
    void RemoveRenderSurfaceExtension(IRenderSurfaceExtension& extension) noexcept;
    bool IsOwnerThread() const noexcept { return std::this_thread::get_id() == ownerThread_; }
    static World* ResolveRuntimeWorld(uint64_t identity, RuntimeWorldAccessResult* outAccess = nullptr) noexcept;
    bool CanMutateStructure() const noexcept {
        return IsOwnerThread() && structureReadDepth_ == 0u && !worldClosing_ &&
               ((!commandFlushActive_ && pendingCommands_.empty()) || commandApplyActive_);
    }
    [[nodiscard]] StructureReadScope AcquireStructureReadScope() noexcept { return StructureReadScope(*this); }

    template <class T, class Fn>
    bool ForEachObjectWith(Fn&& fn) {
        static_assert(std::is_base_of_v<IComponent, T>);
        return VisitIndexedComponentType<T>(std::forward<Fn>(fn));
    }

    template <class T1, class T2, class Fn>
    bool ForEachObjectWith(WorldComponentQueryAnchor anchor, Fn&& fn) {
        static_assert(std::is_base_of_v<IComponent, T1>);
        static_assert(std::is_base_of_v<IComponent, T2>);
        if (!IsOwnerThread()) {
            return false;
        }
        const std::array typeIds{ResolveIndexedComponentTypeId<T1>(), ResolveIndexedComponentTypeId<T2>()};
        if (!typeIds[0].IsValid() || !typeIds[1].IsValid()) {
            return false;
        }

        using Callback = std::remove_reference_t<Fn>;
        struct VisitContext {
            World* world = nullptr;
            Callback* callback = nullptr;
            std::exception_ptr exception{};
        };
        VisitContext context{this, std::addressof(fn), {}};
        const WorldComponentQueryPlan plan{typeIds, WorldComponentQueryMatch::All,
                                           WorldComponentQueryCardinality::FirstPerObject, anchor,
                                           WorldComponentQueryOrder::AnchorAttachment};
        const bool completed = VisitIndexedComponents(
            plan, &context,
            [](void* rawContext, RuntimeObjectHandle objectHandle, IComponent* const* components,
               uint32_t componentCount) noexcept {
                VisitContext& visit = *static_cast<VisitContext*>(rawContext);
                try {
                    GameObject* object = visit.world->FindObject(objectHandle);
                    T1* first = componentCount == 2u ? dynamic_cast<T1*>(components[0]) : nullptr;
                    T2* second = componentCount == 2u ? dynamic_cast<T2*>(components[1]) : nullptr;
                    if (object == nullptr || first == nullptr || second == nullptr) {
                        return false;
                    }
                    (*visit.callback)(*object, *first, *second);
                    return true;
                } catch (...) {
                    visit.exception = std::current_exception();
                    return false;
                }
            });
        if (context.exception) {
            std::rethrow_exception(context.exception);
        }
        return completed;
    }

private:
    friend class GameObject;
    friend class PrefabView;
    friend class SystemScheduler;
    friend struct GameProjectSdkAccess;

    using IndexedComponentVisitorFn = bool (*)(void* context, RuntimeObjectHandle object, IComponent* const* components,
                                               uint32_t componentCount) noexcept;
    using MassObjectVisitorFn = bool (*)(void* context, WorldMassMutableRow& row) noexcept;

    template <class T, class Fn>
    bool VisitIndexedComponentType(Fn&& fn) {
        if (!IsOwnerThread()) {
            return false;
        }
        const ComponentTypeId typeId = ResolveIndexedComponentTypeId<T>();
        if (!typeId.IsValid()) {
            return false;
        }
        const std::array typeIds{typeId};

        using Callback = std::remove_reference_t<Fn>;
        struct VisitContext {
            World* world = nullptr;
            Callback* callback = nullptr;
            std::exception_ptr exception{};
        };
        VisitContext context{this, std::addressof(fn), {}};
        const WorldComponentQueryPlan plan{
            typeIds, WorldComponentQueryMatch::All, WorldComponentQueryCardinality::AllEntries,
            WorldComponentQueryAnchor::FirstRequiredType, WorldComponentQueryOrder::AnchorAttachment};
        const bool completed =
            VisitIndexedComponents(plan, &context,
                                   [](void* rawContext, RuntimeObjectHandle objectHandle, IComponent* const* components,
                                      uint32_t componentCount) noexcept {
                                       VisitContext& visit = *static_cast<VisitContext*>(rawContext);
                                       try {
                                           GameObject* object = visit.world->FindObject(objectHandle);
                                           T* typed = componentCount == 1u ? dynamic_cast<T*>(components[0]) : nullptr;
                                           if (object == nullptr || typed == nullptr) {
                                               return false;
                                           }
                                           (*visit.callback)(*object, *typed);
                                           return true;
                                       } catch (...) {
                                           visit.exception = std::current_exception();
                                           return false;
                                       }
                                   });
        if (context.exception) {
            std::rethrow_exception(context.exception);
        }
        return completed;
    }
    bool CanEnterStructureCallback(const GameObject* object) const noexcept {
        return IsOwnerThread() && structureReadDepth_ == 0u &&
               (!commandFlushActive_ || spawnMaterializationObject_ == object) && !worldClosing_;
    }

    bool PrepareComponentAttachment(GameObject& object, IComponent& component, ComponentTypeId declaredTypeId);
    void CancelComponentAttachment(IComponent& component) noexcept;
    bool CommitComponentAttachment(GameObject& object, IComponent& component) noexcept;
    bool AddNativeComponentTypeBinding(IComponent& component, ComponentTypeId typeId) noexcept;
    void RemoveNativeComponentTypeBinding(IComponent& component, ComponentTypeId typeId) noexcept;
    void RemoveComponentFromIndex(IComponent& component) noexcept;
    void RemoveRenderObjectDirty(GameObject* object) noexcept;
    void RollbackRemovedRenderObjectAdmission(size_t checkpoint) noexcept;
    bool VisitIndexedComponents(const WorldComponentQueryPlan& plan, void* context,
                                IndexedComponentVisitorFn visitor) noexcept;
    bool VisitMassObjects(ComponentTypeId typeId, void* context, MassObjectVisitorFn visitor) noexcept;
    WorldMutationResult SetMassTransformIfPresent(RuntimeObjectHandle object, const Transform3D& transform,
                                                  bool& outIsMassObject) noexcept;
    bool TryGetMassTransformIfPresent(RuntimeObjectHandle object, Transform3D& outTransform,
                                      bool& outIsMassObject) const noexcept;

    template <class T>
    ComponentTypeId ResolveIndexedComponentTypeId() const noexcept {
        // Exact native adapter types only. Base-class and multi-ID adapter
        // queries must use an explicit declared ComponentTypeId plan.
        const auto found = nativeComponentTypeIds_.find(std::type_index(typeid(T)));
        return found != nativeComponentTypeIds_.end() ? found->second.resolvedTypeId : ComponentTypeId{};
    }

    struct WorldCommandCheckpoint {
        size_t pendingCommandCount = 0u;
        size_t reservationJournalCount = 0u;
    };

    WorldCommandCheckpoint BeginComponentCallback(GameObject& object);
    void CommitComponentCallback(WorldCommandCheckpoint checkpoint) noexcept;
    void RollbackComponentCallback(WorldCommandCheckpoint checkpoint) noexcept;
    void RollbackCommandCheckpoint(WorldCommandCheckpoint) noexcept;

    enum class ObjectSlotState : uint8_t { Free, Reserved, ReservedCanceled, Live, Retired };

    enum class ObjectStorageKind : uint8_t {
        None,
        GameObject,
        Mass,
    };

    static constexpr uint32_t kInvalidLiveObjectSlot = (std::numeric_limits<uint32_t>::max)();
    static constexpr size_t kInvalidObjectStorageIndex = (std::numeric_limits<size_t>::max)();

    struct ObjectSlot {
        GameObject* object = nullptr;
        size_t storageIndex = kInvalidObjectStorageIndex;
        ComponentTypeId massTypeId{};
        uint32_t previousLiveSlot = kInvalidLiveObjectSlot;
        uint32_t nextLiveSlot = kInvalidLiveObjectSlot;
        uint32_t generation = 1u;
        uint32_t reservationToken = 0u;
        uint64_t prefabInstanceId = 0u;
        uint64_t prefabLocalObjectId = 0u;
        ObjectSlotState state = ObjectSlotState::Free;
        ObjectStorageKind storageKind = ObjectStorageKind::None;
    };

    struct ReservedStateJournalEntry {
        RuntimeObjectHandle object{};
        ObjectSlotState previousState = ObjectSlotState::Reserved;
    };

    struct PrefabRequestRecord {
        GameProjectPrefabRequestResult result{};
        std::shared_ptr<WorldPrefabInstantiatePayload> payload{};
    };

    struct PrefabInstanceRecord {
        RuntimeObjectHandle root{};
        std::vector<RuntimeObjectHandle> objects{};
        std::unordered_map<uint64_t, RuntimeObjectHandle> objectsByPrefabLocalId{};
        bool destroyPending = false;
    };

    struct NativeComponentTypeCount {
        ComponentTypeId typeId{};
        size_t liveCount = 0u;
    };

    struct NativeComponentTypeBinding {
        std::vector<NativeComponentTypeCount> typeCounts{};
        ComponentTypeId resolvedTypeId{};
    };

    RuntimeObjectHandle ReserveObjectHandle();
    bool ReserveObjectHandleBatch(std::span<RuntimeObjectHandle> outHandles) noexcept;
    bool CommitReservedObjectHandle(RuntimeObjectHandle handle, GameObject* object, size_t storageIndex) noexcept;
    bool CommitReservedMassObjectHandle(RuntimeObjectHandle handle, ComponentTypeId typeId,
                                        size_t storageIndex) noexcept;
    void ReleaseReservedObjectHandle(RuntimeObjectHandle handle) noexcept;
    bool IsReservedObjectHandle(RuntimeObjectHandle handle) const noexcept;
    bool IsKnownObjectHandle(RuntimeObjectHandle handle) const noexcept;
    bool TryGetMassObjectLocation(RuntimeObjectHandle handle, ComponentTypeId& outTypeId,
                                  size_t& outStorageIndex) const noexcept;
    void RecycleObjectSlot(uint32_t slotIndex) noexcept;
    void AppendLiveObjectSlot(uint32_t slotIndex) noexcept;
    void RemoveLiveObjectSlot(uint32_t slotIndex) noexcept;
    void ReleaseObjectHandle(RuntimeObjectHandle handle);
    WorldCommandSubmission QueueCommand(PendingWorldCommand command);
    void ReleaseCommandPayloadReservations(const PendingWorldCommand& command) noexcept;
    uint32_t AllocateCommandToken() noexcept;
    void RecordCommandFailure(const PendingWorldCommand& command, WorldMutationResult result,
                              WorldCommandPhase phase) noexcept;
    void RecordParallelMassSystemFailure(WorldCommandPhase phase) noexcept;
    WorldMutationResult ApplyQueuedSpawn(const PendingWorldCommand& command, std::string_view nameOverride = {},
                                         bool attachToLiveObject = false,
                                         uint32_t componentOnlyIndex = (std::numeric_limits<uint32_t>::max)(),
                                         bool preserveReservationJournal = false);
    WorldMutationResult ApplyQueuedPrefabInstantiate(const PendingWorldCommand& command);
    WorldMutationResult ApplyQueuedPrefabDestroyInstance(const PendingWorldCommand& command);
    GameProjectPrefabSubmission RequestPrefabInstantiate(std::string_view prefabAssetGuid,
                                                         const GameProjectPrefabInstantiateOptions& options) noexcept;
    bool ReadPrefabRequestResult(GameProjectPrefabRequestHandle request,
                                 GameProjectPrefabRequestResult& outResult) const noexcept;
    bool CancelPrefabRequest(GameProjectPrefabRequestHandle request) noexcept;
    bool ReleasePrefabRequest(GameProjectPrefabRequestHandle request) noexcept;
    RuntimeObjectHandle FindPrefabInstanceObject(GameProjectPrefabInstanceHandle instance,
                                                 uint64_t prefabLocalObjectId) const noexcept;
    WorldCommandSubmission SubmitDestroyPrefabInstance(uint64_t instanceId);
    void ReleasePrefabSpawnReservations(const WorldPrefabInstantiatePayload& payload) noexcept;
    void FinishPrefabRequest(uint64_t requestId, GameProjectPrefabRequestState state,
                             GameProjectPrefabError error) noexcept;
    bool ReserveDynamicSceneObjectId(RuntimeObjectHandle object, uint64_t& outId) noexcept;
    void ReleaseDynamicSceneObjectId(uint64_t id, RuntimeObjectHandle object) noexcept;
    void ReleasePrefabInstanceObject(uint64_t instanceId, uint64_t prefabLocalObjectId) noexcept;
    WorldMutationResult ApplyQueuedMassSpawnBatch(const PendingWorldCommand& command);
    WorldMutationResult ApplyQueuedMassDestroyBatch(const PendingWorldCommand& command);
    void UnregisterRuntimeIdentity() noexcept;
    bool OnDocumentIdChanged(GameObject* object, SceneObjectId previousId, SceneObjectId nextId);

    std::vector<std::unique_ptr<GameObject>> objects_;
    WorldMassStore massStore_{};
    WorldInstanceStore* instanceStore_ = nullptr;
    std::vector<WorldMassRenderStoreView> instanceRenderViews_{};
    std::vector<ObjectSlot> objectSlots_{};
    std::vector<uint32_t> freeObjectSlots_{};
    uint32_t firstLiveObjectSlot_ = kInvalidLiveObjectSlot;
    uint32_t lastLiveObjectSlot_ = kInvalidLiveObjectSlot;
    size_t liveObjectCount_ = 0u;
    std::unordered_map<uint64_t, GameObject*> objectsByDocumentId_{};
    std::unordered_map<uint64_t, RuntimeObjectHandle> reservedDynamicSceneObjectIds_{};
    std::unordered_map<uint64_t, PrefabRequestRecord> prefabRequests_{};
    std::unordered_map<uint64_t, PrefabInstanceRecord> prefabInstances_{};
    std::unordered_map<uint64_t, WorldComponentBucket> componentBuckets_{};
    // Invalid values mark one native C++ adapter type that represents
    // multiple declared component IDs; such callers must query by ID.
    std::unordered_map<std::type_index, NativeComponentTypeBinding> nativeComponentTypeIds_{};
    std::vector<GameObject*> renderDirtyObjects_{};
    size_t renderTrackedObjectCount_ = 0u;
    std::vector<uint64_t> removedRenderObjectIds_{};
    std::unordered_set<uint64_t> removedRenderObjectIdSet_{};
    WorldServiceRegistry services_{};
    WorldEventStream frameEvents_{};
    WorldEventStream fixedEvents_{};
    std::vector<PendingWorldCommand> pendingCommands_{};
    std::vector<PendingWorldCommand> executionCommands_{};
    const ComponentRegistry* componentRegistry_ = nullptr;
    const WorldMassSchemaRegistry* massSchemaRegistry_ = nullptr;
    std::vector<WorldCommandFailure> commandFailures_{};
    std::vector<ReservedStateJournalEntry> reservationStateJournal_{};
    std::thread::id ownerThread_{};
    uint64_t runtimeIdentity_ = 0u;
    std::vector<IRenderSurfaceExtension*> renderSurfaceExtensions_{};
    std::vector<INavigationGeometrySource*> navigationGeometrySources_{};
    std::vector<IWaterSource*> waterSources_{};
    uint64_t nextPrefabRequestId_ = 1u;
    uint64_t nextPrefabInstanceId_ = 1u;
    uint64_t nextDynamicSceneObjectId_ = 1u;
    uint64_t nextCommandSequence_ = 1u;
    uint32_t nextCommandToken_ = 1u;
    uint32_t commandFailureOverflowCount_ = 0u;
    uint32_t structureReadDepth_ = 0u;
    bool commandFlushActive_ = false;
    bool commandApplyActive_ = false;
    GameObject* spawnMaterializationObject_ = nullptr;
    bool componentCallbackActive_ = false;
    bool worldClosing_ = false;
};

} // namespace HIKARI
