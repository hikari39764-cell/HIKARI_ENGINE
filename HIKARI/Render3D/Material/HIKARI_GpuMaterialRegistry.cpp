#include "Render3D/Material/HIKARI_GpuMaterialRegistry.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace HIKARI::RENDER3D::MATERIAL {

    namespace {
        size_t HashCombine(size_t seed, uint64_t value) {
            return seed ^
                (static_cast<size_t>(value) +
                    static_cast<size_t>(0x9e3779b97f4a7c15ull) +
                    (seed << 6u) +
                    (seed >> 2u));
        }

        GpuMaterialData BuildDefaultMaterialData() {
            GpuMaterialData data{};
            data.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
            data.pbrParams = { 0.0f, 1.0f, 1.0f, 0.5f };
            data.specularParams = { 1.0f, 1.0f, 1.0f, 1.0f };
            return data;
        }
    }

    const char* ToString(GpuMaterialSourceSyncMode mode) {
        switch (mode) {
        case GpuMaterialSourceSyncMode::None: return "Reuse";
        case GpuMaterialSourceSyncMode::Full: return "Full";
        case GpuMaterialSourceSyncMode::Incremental: return "Incremental";
        case GpuMaterialSourceSyncMode::Retry: return "Retry";
        default: return "Unknown";
        }
    }

    size_t GpuMaterialRegistry::MaterialIdentityHash::operator()(
        const MaterialIdentity& value) const {

        size_t seed = 1469598103934665603ull;
        seed = HashCombine(seed, value.resource.index);
        seed = HashCombine(seed, value.resource.generation);
        seed = HashCombine(seed, value.stableMaterialKey);
        return seed;
    }

    bool GpuMaterialRegistry::Initialize(
        uint32_t capacity,
        uint32_t frameResourceCount) {

        if (capacity == 0u ||
            frameResourceCount == 0u ||
            frameResourceCount >= 32u) {
            return false;
        }

        if (initialized_) {
            return capacity_ == capacity &&
                frameResourceCount_ == frameResourceCount;
        }

        capacity_ = capacity;
        frameResourceCount_ = frameResourceCount;
        initialized_ = true;
        slots_.reserve(capacity_);
        pendingSlotsByFrame_.resize(frameResourceCount_);
        RecreateDefaultSlot();
        RefreshStats();
        return true;
    }

    void GpuMaterialRegistry::Clear() {
        slots_.clear();
        freeSlots_.clear();
        slotByIdentity_.clear();
        sourceBindings_.clear();
        sourceSlotTable_.clear();
        pendingSlotsByFrame_.assign(frameResourceCount_, {});
        dataVersion_ = 0;
        bindingVersion_ = 0;
        sourceIdentity_ = 0;
        sourceLayoutVersion_ = 0;
        sourceVersion_ = 0;
        sourceStateValid_ = false;
        syncMode_ = GpuMaterialSourceSyncMode::None;
        residentSlotCount_ = 0;
        sourceBindingCount_ = 0;
        pendingSourceResolveCount_ = 0;
        pendingFrameSlotCount_ = 0;
        stats_ = {};
        if (initialized_) {
            RecreateDefaultSlot();
        }
        RefreshStats();
    }

    void GpuMaterialRegistry::BeginFrame() {
        stats_.sourceSyncMode = GpuMaterialSourceSyncMode::None;
        stats_.frameResolveRequestCount = 0;
        stats_.frameSourceReuseCount = 0;
        stats_.frameCreatedSlotCount = 0;
        stats_.frameUpdatedSlotCount = 0;
        stats_.frameRetiredSlotCount = 0;
        stats_.frameOverflowCount = 0;
        stats_.frameUploadedSlotCount = 0;
        stats_.frameUploadRangeCount = 0;
        stats_.frameUploadBytes = 0;
        RefreshStats();
    }

    GpuMaterialSourceSyncMode GpuMaterialRegistry::BeginSourceSync(
        uintptr_t sourceIdentity,
        uint64_t layoutVersion,
        uint64_t sourceVersion,
        uint64_t dirtyBaseSourceVersion,
        uint32_t sourceRecordCount) {

        if (!initialized_) {
            syncMode_ = GpuMaterialSourceSyncMode::None;
            return syncMode_;
        }

        const bool topologyChanged =
            !sourceStateValid_ ||
            sourceIdentity_ != sourceIdentity ||
            sourceLayoutVersion_ != layoutVersion ||
            sourceBindings_.size() != sourceRecordCount ||
            sourceSlotTable_.size() != sourceRecordCount;

        if (topologyChanged) {
            syncMode_ = GpuMaterialSourceSyncMode::Full;
        } else if (sourceVersion_ != sourceVersion) {
            syncMode_ =
                dirtyBaseSourceVersion == sourceVersion_
                    ? GpuMaterialSourceSyncMode::Incremental
                    : GpuMaterialSourceSyncMode::Full;
        } else if (pendingSourceResolveCount_ != 0u) {
            syncMode_ = GpuMaterialSourceSyncMode::Retry;
        } else {
            syncMode_ = GpuMaterialSourceSyncMode::None;
        }

        sourceIdentity_ = sourceIdentity;
        sourceLayoutVersion_ = layoutVersion;
        sourceVersion_ = sourceVersion;
        sourceStateValid_ = true;
        stats_.sourceSyncMode = syncMode_;

        if (syncMode_ == GpuMaterialSourceSyncMode::Full) {
            if (topologyChanged) {
                sourceBindings_.assign(sourceRecordCount, {});
                sourceSlotTable_.assign(
                    sourceRecordCount,
                    kInvalidGpuMaterialIndex);
                ++bindingVersion_;
            }
            for (Slot& slot : slots_) {
                slot.sourceReferenceCount = 0u;
            }
            sourceBindingCount_ = 0u;
            pendingSourceResolveCount_ = 0u;
            for (SourceBinding& binding : sourceBindings_) {
                binding.seenInFullSync = false;
            }
            ++stats_.fullSourceSyncCount;
        } else if (syncMode_ == GpuMaterialSourceSyncMode::Incremental) {
            ++stats_.incrementalSourceSyncCount;
        } else if (syncMode_ == GpuMaterialSourceSyncMode::Retry) {
            ++stats_.retrySourceSyncCount;
        } else {
            ++stats_.stableFrameReuseCount;
        }

        RefreshStats();
        return syncMode_;
    }

    bool GpuMaterialRegistry::TryReuseSourceBinding(
        uint32_t sourceRecordIndex,
        const GpuMaterialSourceKey& sourceKey,
        uint32_t& outMaterialSlot) {

        outMaterialSlot = kInvalidGpuMaterialIndex;
        if (sourceRecordIndex >= sourceBindings_.size()) {
            return false;
        }

        SourceBinding& binding = sourceBindings_[sourceRecordIndex];
        const uint32_t materialSlot = sourceSlotTable_[sourceRecordIndex];
        if (!binding.active ||
            !binding.finalized ||
            materialSlot == kInvalidGpuMaterialIndex ||
            materialSlot >= slots_.size() ||
            !slots_[materialSlot].alive ||
            !(binding.key == sourceKey)) {
            return false;
        }

        MarkSourceSeen(sourceRecordIndex, materialSlot);
        outMaterialSlot = materialSlot;
        ++stats_.frameSourceReuseCount;
        return true;
    }

    uint32_t GpuMaterialRegistry::ResolveAndBindSource(
        uint32_t sourceRecordIndex,
        const GpuMaterialSourceKey& sourceKey,
        const GpuMaterialData& data,
        bool finalized) {

        ++stats_.frameResolveRequestCount;
        if (sourceRecordIndex >= sourceBindings_.size()) {
            ++stats_.frameOverflowCount;
            return kInvalidGpuMaterialIndex;
        }

        SourceBinding& binding = sourceBindings_[sourceRecordIndex];
        const uint32_t oldSlot =
            sourceSlotTable_[sourceRecordIndex] < slots_.size() &&
                slots_[sourceSlotTable_[sourceRecordIndex]].alive
                ? sourceSlotTable_[sourceRecordIndex]
                : kInvalidGpuMaterialIndex;
        const bool oldBound =
            binding.active && oldSlot != kInvalidGpuMaterialIndex;
        const bool oldPending = binding.active && !binding.finalized;
        const MaterialIdentity identity{
            sourceKey.resource,
            sourceKey.stableMaterialKey
        };
        const uint32_t newSlot = ResolveSlot(identity, data);
        if (newSlot == kInvalidGpuMaterialIndex) {
            if (syncMode_ != GpuMaterialSourceSyncMode::Full) {
                ReleaseSourceReference(oldSlot);
                if (oldBound && sourceBindingCount_ != 0u) {
                    --sourceBindingCount_;
                }
                if (!oldPending) {
                    ++pendingSourceResolveCount_;
                }
            }
            binding.key = sourceKey;
            binding.active = true;
            binding.finalized = false;
            sourceSlotTable_[sourceRecordIndex] = kInvalidGpuMaterialIndex;
            if (oldSlot != kInvalidGpuMaterialIndex) {
                ++bindingVersion_;
            }
            MarkSourceSeen(sourceRecordIndex, kInvalidGpuMaterialIndex);
            return newSlot;
        }

        if (syncMode_ != GpuMaterialSourceSyncMode::Full &&
            oldSlot != newSlot) {
            ReleaseSourceReference(oldSlot);
            ++slots_[newSlot].sourceReferenceCount;
        }

        if (oldSlot != newSlot) {
            ++bindingVersion_;
        }
        binding.key = sourceKey;
        sourceSlotTable_[sourceRecordIndex] = newSlot;
        binding.active = true;
        binding.finalized = finalized;
        if (syncMode_ == GpuMaterialSourceSyncMode::Full) {
            MarkSourceSeen(sourceRecordIndex, newSlot);
        } else {
            const bool newPending = !finalized;
            if (!oldBound) {
                ++sourceBindingCount_;
            }
            if (oldPending != newPending) {
                if (newPending) {
                    ++pendingSourceResolveCount_;
                } else if (pendingSourceResolveCount_ != 0u) {
                    --pendingSourceResolveCount_;
                }
            }
        }
        return newSlot;
    }

    void GpuMaterialRegistry::EndSourceSync() {
        if (syncMode_ == GpuMaterialSourceSyncMode::Full) {
            bool bindingsChanged = false;
            for (size_t sourceIndex = 0;
                sourceIndex < sourceBindings_.size();
                ++sourceIndex) {

                SourceBinding& binding = sourceBindings_[sourceIndex];
                if (binding.seenInFullSync) {
                    continue;
                }
                if (sourceSlotTable_[sourceIndex] != kInvalidGpuMaterialIndex) {
                    bindingsChanged = true;
                }
                binding = {};
                sourceSlotTable_[sourceIndex] = kInvalidGpuMaterialIndex;
            }
            if (bindingsChanged) {
                ++bindingVersion_;
            }
        }

        RetireUnusedSlots();
        syncMode_ = GpuMaterialSourceSyncMode::None;
        RefreshStats();
    }

    bool GpuMaterialRegistry::StageFrame(
        uint32_t frameResourceIndex,
        GpuMaterialData* target,
        size_t targetCapacity,
        std::vector<GpuMaterialUploadRange>& outRanges) {

        outRanges.clear();
        if (!initialized_ ||
            target == nullptr ||
            frameResourceIndex >= pendingSlotsByFrame_.size()) {
            return false;
        }

        std::vector<uint32_t>& pending = pendingSlotsByFrame_[frameResourceIndex];
        if (pending.empty()) {
            RefreshStats();
            return true;
        }

        std::sort(pending.begin(), pending.end());
        pending.erase(std::unique(pending.begin(), pending.end()), pending.end());

        const uint32_t frameBit = 1u << frameResourceIndex;
        std::vector<uint32_t> remaining{};
        remaining.reserve(pending.size());
        std::vector<uint32_t> uploaded{};
        uploaded.reserve(pending.size());

        for (const uint32_t slotIndex : pending) {
            if (slotIndex >= slots_.size()) {
                continue;
            }
            Slot& slot = slots_[slotIndex];
            if (!slot.alive) {
                if ((slot.pendingFrameMask & frameBit) != 0u) {
                    slot.pendingFrameMask &= ~frameBit;
                    if (pendingFrameSlotCount_ != 0u) {
                        --pendingFrameSlotCount_;
                    }
                }
                continue;
            }
            if (slotIndex >= targetCapacity) {
                remaining.push_back(slotIndex);
                ++stats_.frameOverflowCount;
                continue;
            }

            target[slotIndex] = slot.data;
            uploaded.push_back(slotIndex);
            if ((slot.pendingFrameMask & frameBit) != 0u) {
                slot.pendingFrameMask &= ~frameBit;
                if (pendingFrameSlotCount_ != 0u) {
                    --pendingFrameSlotCount_;
                }
            }
        }
        pending = std::move(remaining);

        for (const uint32_t slotIndex : uploaded) {
            if (outRanges.empty() ||
                outRanges.back().firstSlot + outRanges.back().slotCount != slotIndex) {
                outRanges.push_back({ slotIndex, 1u });
            } else {
                ++outRanges.back().slotCount;
            }
        }

        stats_.frameUploadedSlotCount = static_cast<uint32_t>(uploaded.size());
        stats_.frameUploadRangeCount = static_cast<uint32_t>(outRanges.size());
        stats_.frameUploadBytes = uploaded.size() * sizeof(GpuMaterialData);
        RefreshStats();
        return true;
    }

    std::span<const uint32_t> GpuMaterialRegistry::GetSourceBindings() const {
        return { sourceSlotTable_.data(), sourceSlotTable_.size() };
    }

    uint64_t GpuMaterialRegistry::GetBindingVersion() const {
        return bindingVersion_;
    }

    const GpuMaterialRegistryStats& GpuMaterialRegistry::GetStats() const {
        return stats_;
    }

    uint32_t GpuMaterialRegistry::ResolveSlot(
        const MaterialIdentity& identity,
        const GpuMaterialData& data) {

        const auto found = slotByIdentity_.find(identity);
        if (found != slotByIdentity_.end() &&
            found->second < slots_.size() &&
            slots_[found->second].alive) {

            Slot& slot = slots_[found->second];
            if (std::memcmp(&slot.data, &data, sizeof(data)) != 0) {
                slot.data = data;
                MarkSlotDirty(found->second);
                ++dataVersion_;
                ++stats_.frameUpdatedSlotCount;
            }
            return found->second;
        }

        const uint32_t slotIndex = AllocateSlot();
        if (slotIndex == kInvalidGpuMaterialIndex) {
            ++stats_.frameOverflowCount;
            return slotIndex;
        }

        Slot& slot = slots_[slotIndex];
        slot = {};
        slot.identity = identity;
        slot.data = data;
        slot.alive = true;
        slotByIdentity_[identity] = slotIndex;
        ++residentSlotCount_;
        MarkSlotDirty(slotIndex);
        ++dataVersion_;
        ++stats_.frameCreatedSlotCount;
        return slotIndex;
    }

    uint32_t GpuMaterialRegistry::AllocateSlot() {
        if (!freeSlots_.empty()) {
            const uint32_t slot = freeSlots_.back();
            freeSlots_.pop_back();
            return slot;
        }
        if (slots_.size() >= capacity_ ||
            slots_.size() >= static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
            return kInvalidGpuMaterialIndex;
        }
        const uint32_t slot = static_cast<uint32_t>(slots_.size());
        slots_.push_back({});
        return slot;
    }

    void GpuMaterialRegistry::MarkSlotDirty(uint32_t slotIndex) {
        if (slotIndex >= slots_.size()) {
            return;
        }
        Slot& slot = slots_[slotIndex];
        for (uint32_t frameIndex = 0;
            frameIndex < frameResourceCount_;
            ++frameIndex) {

            const uint32_t frameBit = 1u << frameIndex;
            if ((slot.pendingFrameMask & frameBit) != 0u) {
                continue;
            }
            slot.pendingFrameMask |= frameBit;
            pendingSlotsByFrame_[frameIndex].push_back(slotIndex);
            ++pendingFrameSlotCount_;
        }
    }

    void GpuMaterialRegistry::MarkSourceSeen(
        uint32_t sourceRecordIndex,
        uint32_t materialSlot) {

        if (sourceRecordIndex >= sourceBindings_.size()) {
            return;
        }
        SourceBinding& binding = sourceBindings_[sourceRecordIndex];
        if (syncMode_ == GpuMaterialSourceSyncMode::Full &&
            !binding.seenInFullSync) {
            binding.seenInFullSync = true;
            if (materialSlot < slots_.size() && slots_[materialSlot].alive) {
                ++slots_[materialSlot].sourceReferenceCount;
                ++sourceBindingCount_;
            }
            if (binding.active && !binding.finalized) {
                ++pendingSourceResolveCount_;
            }
        }
    }

    void GpuMaterialRegistry::ReleaseSourceReference(uint32_t materialSlot) {
        if (materialSlot == kInvalidGpuMaterialIndex ||
            materialSlot >= slots_.size()) {
            return;
        }
        Slot& slot = slots_[materialSlot];
        if (slot.sourceReferenceCount != 0u) {
            --slot.sourceReferenceCount;
        }
    }

    void GpuMaterialRegistry::RetireUnusedSlots() {
        for (uint32_t slotIndex = 1u;
            slotIndex < slots_.size();
            ++slotIndex) {

            Slot& slot = slots_[slotIndex];
            if (!slot.alive || slot.pinned || slot.sourceReferenceCount != 0u) {
                continue;
            }
            slotByIdentity_.erase(slot.identity);
            pendingFrameSlotCount_ -=
                (std::min)(
                    pendingFrameSlotCount_,
                    static_cast<uint32_t>(std::popcount(slot.pendingFrameMask)));
            slot.pendingFrameMask = 0u;
            slot.alive = false;
            slot.identity = {};
            slot.data = {};
            freeSlots_.push_back(slotIndex);
            if (residentSlotCount_ != 0u) {
                --residentSlotCount_;
            }
            ++stats_.frameRetiredSlotCount;
        }
    }

    void GpuMaterialRegistry::RefreshStats() {
        stats_.initialized = initialized_;
        stats_.capacity = capacity_;
        stats_.residentSlotCount = residentSlotCount_;
        stats_.sourceBindingCount = sourceBindingCount_;
        stats_.pendingSourceResolveCount = pendingSourceResolveCount_;
        stats_.pendingFrameSlotCount = pendingFrameSlotCount_;
        stats_.dataVersion = dataVersion_;
        stats_.bindingVersion = bindingVersion_;
    }

    void GpuMaterialRegistry::RecreateDefaultSlot() {
        slots_.push_back({});
        Slot& slot = slots_.front();
        slot.data = BuildDefaultMaterialData();
        slot.alive = true;
        slot.pinned = true;
        residentSlotCount_ = 1u;
        MarkSlotDirty(0u);
        dataVersion_ = 1u;
        bindingVersion_ = 1u;
    }

} // namespace HIKARI::RENDER3D::MATERIAL
