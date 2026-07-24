#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::RENDER3D::MATERIAL {

    enum class GpuMaterialSourceSyncMode : uint8_t {
        None,
        Full,
        Incremental,
        Retry,
    };

    const char* ToString(GpuMaterialSourceSyncMode mode);

    struct GpuMaterialSourceKey {
        MaterialResourceHandle resource{};
        uint64_t stableMaterialKey = 0;
        uintptr_t modelIdentity = 0;
        uintptr_t materialOverrideIdentity = 0;
        uint64_t materialRevision = 0;
        uint32_t materialIndex = 0;

        friend bool operator==(
            const GpuMaterialSourceKey& lhs,
            const GpuMaterialSourceKey& rhs) {
            return
                lhs.resource == rhs.resource &&
                lhs.stableMaterialKey == rhs.stableMaterialKey &&
                lhs.modelIdentity == rhs.modelIdentity &&
                lhs.materialOverrideIdentity == rhs.materialOverrideIdentity &&
                lhs.materialRevision == rhs.materialRevision &&
                lhs.materialIndex == rhs.materialIndex;
        }
    };

    struct GpuMaterialUploadRange {
        uint32_t firstSlot = 0;
        uint32_t slotCount = 0;
    };

    struct GpuMaterialRegistryStats {
        bool initialized = false;
        uint32_t capacity = 0;
        uint32_t residentSlotCount = 0;
        uint32_t sourceBindingCount = 0;
        uint32_t pendingSourceResolveCount = 0;
        uint32_t pendingFrameSlotCount = 0;
        uint64_t dataVersion = 0;
        uint64_t bindingVersion = 0;
        GpuMaterialSourceSyncMode sourceSyncMode = GpuMaterialSourceSyncMode::None;
        uint32_t frameResolveRequestCount = 0;
        uint32_t frameSourceReuseCount = 0;
        uint32_t frameCreatedSlotCount = 0;
        uint32_t frameUpdatedSlotCount = 0;
        uint32_t frameRetiredSlotCount = 0;
        uint32_t frameOverflowCount = 0;
        uint32_t frameUploadedSlotCount = 0;
        uint32_t frameUploadRangeCount = 0;
        size_t frameUploadBytes = 0;
        uint32_t stableFrameReuseCount = 0;
        uint32_t fullSourceSyncCount = 0;
        uint32_t incrementalSourceSyncCount = 0;
        uint32_t retrySourceSyncCount = 0;
    };

    class GpuMaterialRegistry final {
    public:
        bool Initialize(uint32_t capacity, uint32_t frameResourceCount);
        void Clear();
        void BeginFrame();

        GpuMaterialSourceSyncMode BeginSourceSync(
            uintptr_t sourceIdentity,
            uint64_t layoutVersion,
            uint64_t sourceVersion,
            uint64_t dirtyBaseSourceVersion,
            uint32_t sourceRecordCount);

        bool TryReuseSourceBinding(
            uint32_t sourceRecordIndex,
            const GpuMaterialSourceKey& sourceKey,
            uint32_t& outMaterialSlot);

        uint32_t ResolveAndBindSource(
            uint32_t sourceRecordIndex,
            const GpuMaterialSourceKey& sourceKey,
            const GpuMaterialData& data,
            bool finalized);

        void EndSourceSync();

        bool StageFrame(
            uint32_t frameResourceIndex,
            GpuMaterialData* target,
            size_t targetCapacity,
            std::vector<GpuMaterialUploadRange>& outRanges);

        std::span<const uint32_t> GetSourceBindings() const;
        uint64_t GetBindingVersion() const;
        const GpuMaterialRegistryStats& GetStats() const;

    private:
        struct MaterialIdentity {
            MaterialResourceHandle resource{};
            uint64_t stableMaterialKey = 0;

            friend bool operator==(
                const MaterialIdentity& lhs,
                const MaterialIdentity& rhs) {
                return
                    lhs.resource == rhs.resource &&
                    lhs.stableMaterialKey == rhs.stableMaterialKey;
            }
        };

        struct MaterialIdentityHash {
            size_t operator()(const MaterialIdentity& value) const;
        };

        struct Slot {
            MaterialIdentity identity{};
            GpuMaterialData data{};
            uint32_t sourceReferenceCount = 0;
            uint32_t pendingFrameMask = 0;
            bool alive = false;
            bool pinned = false;
        };

        struct SourceBinding {
            GpuMaterialSourceKey key{};
            bool active = false;
            bool finalized = false;
            bool seenInFullSync = false;
        };

        uint32_t ResolveSlot(
            const MaterialIdentity& identity,
            const GpuMaterialData& data);
        uint32_t AllocateSlot();
        void MarkSlotDirty(uint32_t slotIndex);
        void MarkSourceSeen(uint32_t sourceRecordIndex, uint32_t materialSlot);
        void ReleaseSourceReference(uint32_t materialSlot);
        void RetireUnusedSlots();
        void RefreshStats();
        void RecreateDefaultSlot();

        uint32_t capacity_ = 0;
        uint32_t frameResourceCount_ = 0;
        uint64_t dataVersion_ = 0;
        uint64_t bindingVersion_ = 0;
        uintptr_t sourceIdentity_ = 0;
        uint64_t sourceLayoutVersion_ = 0;
        uint64_t sourceVersion_ = 0;
        bool sourceStateValid_ = false;
        bool initialized_ = false;
        GpuMaterialSourceSyncMode syncMode_ = GpuMaterialSourceSyncMode::None;
        uint32_t residentSlotCount_ = 0;
        uint32_t sourceBindingCount_ = 0;
        uint32_t pendingSourceResolveCount_ = 0;
        uint32_t pendingFrameSlotCount_ = 0;

        std::vector<Slot> slots_{};
        std::vector<uint32_t> freeSlots_{};
        std::unordered_map<MaterialIdentity, uint32_t, MaterialIdentityHash> slotByIdentity_{};
        std::vector<SourceBinding> sourceBindings_{};
        std::vector<uint32_t> sourceSlotTable_{};
        std::vector<std::vector<uint32_t>> pendingSlotsByFrame_{};
        GpuMaterialRegistryStats stats_{};
    };

} // namespace HIKARI::RENDER3D::MATERIAL
