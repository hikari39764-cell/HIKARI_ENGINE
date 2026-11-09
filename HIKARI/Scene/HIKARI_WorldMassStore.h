#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "HIKARI/Project/WorldMassTypes.h"
#include "HIKARI/Runtime/ComponentTypeId.h"
#include "Render3D/Runtime/HIKARI_MassRenderProxy.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {

inline constexpr uint32_t kWorldMassMaximumPodBytes = 1024u;

struct WorldMassSchema {
    ComponentTypeId typeId{};
    uint64_t layoutHash = 0u;
    uint32_t schemaVersion = 0u;
    uint32_t podSize = 0u;
    uint32_t podAlignment = 0u;
};

bool IsValidWorldMassSchema(const WorldMassSchema& schema) noexcept;
bool IsSameWorldMassSchema(const WorldMassSchema& lhs, const WorldMassSchema& rhs) noexcept;
bool IsValidWorldMassRenderProxy(const GameProjectWorldMassRenderProxyDesc& descriptor) noexcept;
bool IsSameWorldMassRenderProxy(const GameProjectWorldMassRenderProxyDesc& lhs,
                                const GameProjectWorldMassRenderProxyDesc& rhs) noexcept;

namespace INTERNAL {

bool IsFiniteWorldMassVector(const GameProjectFloat3& value) noexcept;
bool IsValidWorldMassTransform(const GameProjectFloat3& position, const GameProjectQuaternion& rotation,
                               const GameProjectFloat3& scale) noexcept;

} // namespace INTERNAL

class WorldMassSchemaRegistry {
public:
    bool Register(WorldMassSchema schema) noexcept;
    bool RegisterRenderProxy(const GameProjectWorldMassRenderProxyDesc& descriptor) noexcept;
    void Clear() noexcept;
    const WorldMassSchema* Find(ComponentTypeId typeId) const noexcept;
    const GameProjectWorldMassRenderProxyDesc* FindRenderProxy(ComponentTypeId typeId) const noexcept;

private:
    std::unordered_map<uint64_t, WorldMassSchema> schemas_{};
    std::unordered_map<uint64_t, GameProjectWorldMassRenderProxyDesc> renderProxies_{};
};

struct WorldMassSpawnBatchRequest {
    WorldMassSchema schema{};
    std::span<const GameProjectTransform> transforms{};
    std::span<const GameProjectFloat3> velocities{};
    const std::byte* podData = nullptr;
    uint32_t podStride = 0u;
    size_t podDataByteCount = 0u;
};

struct WorldMassMutableRow {
    RuntimeObjectHandle object{};
    GameProjectFloat3* position = nullptr;
    GameProjectQuaternion* rotation = nullptr;
    GameProjectFloat3* scale = nullptr;
    GameProjectFloat3* velocity = nullptr;
    std::byte* podData = nullptr;
    uint32_t podSize = 0u;
    uint32_t podAlignment = 0u;
};

using WorldMassRenderInstance = RENDER3D::RUNTIME::MassRenderTransform;

using WorldMassRenderDirtyRange = RENDER3D::RUNTIME::MassRenderDirtyRange;

struct WorldMassRenderStoreView {
    ComponentTypeId typeId{};
    const GameProjectWorldMassRenderProxyDesc* proxy = nullptr;
    std::span<const WorldMassRenderInstance> instances{};
    std::span<const WorldMassRenderDirtyRange> dirtyRanges{};
    uint64_t layoutVersion = 0u;
    uint64_t sourceVersion = 0u;
    uint64_t dirtyBaseSourceVersion = 0u;
    // 遠景模型（無ければ nullptr）。farDistance より遠い個体をこちらで描く。
    const GameProjectWorldMassRenderProxyDesc* farProxy = nullptr;
    float farDistance = 0.0f;
};

struct WorldMassRenderPublicationReceipt {
    uint64_t worldIdentity = 0u;
    uint64_t generation = 0u;
    uint64_t frameIndex = 0u;

    bool IsValid() const noexcept { return worldIdentity != 0u && generation != 0u; }
};

// Owner-thread scheduling seam. The scheduler copies this borrowed view
// into value-owned worker snapshots before dispatching any job.
struct WorldMassSchedulingSnapshotView {
    WorldMassSchema schema{};
    size_t podStride = 0u;
    uint64_t mutationVersion = 0u;
    std::span<const RuntimeObjectHandle> objects{};
    std::span<const GameProjectFloat3> positions{};
    std::span<const GameProjectQuaternion> rotations{};
    std::span<const GameProjectFloat3> scales{};
    std::span<const GameProjectFloat3> velocities{};
    std::span<const std::byte> podBytes{};
};

struct WorldMassSchedulingOutputView {
    size_t firstRow = 0u;
    std::span<const RuntimeObjectHandle> expectedObjects{};
    std::span<const GameProjectFloat3> positions{};
    std::span<const GameProjectQuaternion> rotations{};
    std::span<const GameProjectFloat3> scales{};
    std::span<const GameProjectFloat3> velocities{};
    std::span<const std::byte> podBytes{};
    uint32_t writeMask = 0u;
};

class WorldMassStore {
public:
    using MutableVisitorFn = bool (*)(void* context, size_t rowIndex, WorldMassMutableRow& row) noexcept;
    using RenderPublicationVisitorFn = bool (*)(void* context, const WorldMassRenderPublicationReceipt& receipt,
                                                std::span<const WorldMassRenderStoreView> stores) noexcept;

    WorldMassStore() = default;
    ~WorldMassStore() = default;

private:
    friend class World;
    friend class SystemScheduler;

    bool CanUseSchema(const WorldMassSchema& schema) const noexcept;
    bool AppendBatch(const WorldMassSchema& schema, const GameProjectWorldMassRenderProxyDesc* renderProxy,
                     std::span<const RuntimeObjectHandle> objects, std::span<const GameProjectTransform> transforms,
                     std::span<const GameProjectFloat3> velocities, const std::byte* podData, size_t podStride,
                     std::span<const uint32_t> sourceIndices, size_t& outFirstRowIndex);
    bool RemoveBackBatch(const WorldMassSchema& schema, size_t firstRowIndex,
                         std::span<const RuntimeObjectHandle> objects) noexcept;
    bool Remove(ComponentTypeId typeId, size_t rowIndex, RuntimeObjectHandle object,
                RuntimeObjectHandle& outMovedObject) noexcept;
    bool VisitMutable(ComponentTypeId typeId, void* context, MutableVisitorFn visitor) noexcept;
    bool TryGetTransform(ComponentTypeId typeId, size_t rowIndex, RuntimeObjectHandle object,
                         GameProjectTransform& outTransform) const noexcept;
    bool SetTransform(ComponentTypeId typeId, size_t rowIndex, RuntimeObjectHandle object,
                      const GameProjectTransform& transform) noexcept;
    size_t GetCount(ComponentTypeId typeId) const noexcept;
    size_t GetTotalCount() const noexcept;
    // extraViews は Mass の行を持たない描画の種類（実例集）。確認と汚れの管理は持ち主の側でする。
    bool PublishRenderStores(uint64_t worldIdentity, uint64_t frameIndex,
                             std::span<const WorldMassRenderStoreView> extraViews, void* context,
                             RenderPublicationVisitorFn visitor,
                             WorldMassRenderPublicationReceipt& outReceipt) noexcept;
    // Primary-view v1 is latest-publication-wins. A successful newer
    // publication invalidates an older unacknowledged receipt, but dirty
    // rows remain admitted until the current receipt is acknowledged.
    bool AcknowledgeRenderStores(const WorldMassRenderPublicationReceipt& receipt) noexcept;
    void Clear() noexcept;

    bool TryGetObject(ComponentTypeId typeId, size_t rowIndex, RuntimeObjectHandle& outObject) const noexcept;
    bool GetSchedulingSnapshot(ComponentTypeId typeId, WorldMassSchedulingSnapshotView& outView) const noexcept;
    bool ApplySchedulingOutputs(ComponentTypeId typeId, uint64_t expectedMutationVersion,
                                std::span<const WorldMassSchedulingOutputView> outputs) noexcept;

    struct Store {
        WorldMassSchema schema{};
        size_t podStride = 0u;
        std::vector<RuntimeObjectHandle> objects{};
        std::vector<GameProjectFloat3> positions{};
        std::vector<GameProjectQuaternion> rotations{};
        std::vector<GameProjectFloat3> scales{};
        std::vector<GameProjectFloat3> velocities{};
        std::vector<std::byte> podBytes{};
        GameProjectWorldMassRenderProxyDesc renderProxy{};
        std::vector<WorldMassRenderInstance> renderInstances{};
        std::vector<uint32_t> renderDirtyIndices{};
        std::vector<uint32_t> renderDirtyListPositions{};
        std::vector<WorldMassRenderDirtyRange> renderDirtyRanges{};
        uint64_t renderLayoutVersion = 1u;
        uint64_t renderSourceVersion = 1u;
        uint64_t renderDirtyBaseSourceVersion = 1u;
        uint64_t publishedSourceVersion = 0u;
        uint64_t publishedGeneration = 0u;
        uint64_t mutationVersion = 1u;
        bool renderDirtyIndicesSorted = true;
        bool renderDirtyAll = false;
        bool hasRenderProxy = false;
    };

    bool RemoveFromStore(Store& store, size_t rowIndex, RuntimeObjectHandle object,
                         RuntimeObjectHandle& outMovedObject) noexcept;
    static bool TryGetTransformFromStore(const Store& store, size_t rowIndex, RuntimeObjectHandle object,
                                         GameProjectTransform& outTransform) noexcept;
    static bool SetTransformInStore(Store& store, size_t rowIndex, RuntimeObjectHandle object,
                                    const GameProjectTransform& transform) noexcept;
    static size_t AlignUp(size_t value, size_t alignment) noexcept;
    static WorldMassRenderInstance BuildRenderInstance(RuntimeObjectHandle object, const GameProjectFloat3& position,
                                                       const GameProjectQuaternion& rotation,
                                                       const GameProjectFloat3& scale) noexcept;
    static void MarkRenderDirty(Store& store, size_t rowIndex) noexcept;
    static void RemoveRenderDirty(Store& store, size_t rowIndex) noexcept;
    static void AdvanceRenderSourceVersion(Store& store) noexcept;
    static void AdvanceRenderLayoutVersion(Store& store) noexcept;
    static void AdvanceMutationVersion(Store& store) noexcept;
    static void RebuildRenderDirtyRanges(Store& store);
    static void ClearAcknowledgedRenderDirty(Store& store) noexcept;
    static bool HasChangedTransform(const GameProjectFloat3& position, const GameProjectQuaternion& rotation,
                                    const GameProjectFloat3& scale, const WorldMassRenderInstance& before) noexcept;
    Store* FindStore(ComponentTypeId typeId) noexcept;
    const Store* FindStore(ComponentTypeId typeId) const noexcept;

    std::unordered_map<uint64_t, Store> stores_{};
    std::vector<uint64_t> renderStoreTypeOrder_{};
    std::vector<WorldMassRenderStoreView> renderPublicationViews_{};
    size_t totalCount_ = 0u;
    uint64_t nextRenderPublicationGeneration_ = 1u;
    WorldMassRenderPublicationReceipt activeRenderPublication_{};
    bool renderPublicationInProgress_ = false;
};

} // namespace HIKARI
