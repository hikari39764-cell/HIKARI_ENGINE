#include "Render3D/Material/HIKARI_GpuMaterialRegistry.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

#include "Core/Hashing/HIKARI_FnvHash.h"

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

        size_t seed = HASH::kFnv1a64OffsetBasis;
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

        if (!programBlockStore_.Initialize(capacity, frameResourceCount)) {
            return false;
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
        sourceStates_.clear();
        pendingSlotsByFrame_.assign(frameResourceCount_, {});
        programBlockStore_.Clear();
        dataVersion_ = 0;
        bindingVersionCounter_ = 0;
        activeSourceState_ = nullptr;
        syncMode_ = GpuMaterialSourceSyncMode::None;
        residentSlotCount_ = 0;
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

        auto [sourceIt, inserted] = sourceStates_.try_emplace(sourceIdentity);
        SourceState& source = sourceIt->second;
        const bool bindingStorageChanged =
            inserted ||
            !source.valid ||
            source.bindings.size() != sourceRecordCount ||
            source.slotTable.size() != sourceRecordCount;
        const bool layoutChanged =
            source.layoutVersion != layoutVersion;

        if (bindingStorageChanged || layoutChanged) {
            syncMode_ = GpuMaterialSourceSyncMode::Full;
        } else if (source.sourceVersion != sourceVersion) {
            syncMode_ =
                dirtyBaseSourceVersion == source.sourceVersion
                    ? GpuMaterialSourceSyncMode::Incremental
                    : GpuMaterialSourceSyncMode::Full;
        } else if (std::any_of(
                source.bindings.begin(),
                source.bindings.end(),
                [](const SourceBinding& binding) {
                    return binding.active && !binding.finalized;
                })) {
            syncMode_ = GpuMaterialSourceSyncMode::Retry;
        } else {
            syncMode_ = GpuMaterialSourceSyncMode::None;
        }

        activeSourceState_ = &source;
        source.layoutVersion = layoutVersion;
        source.sourceVersion = sourceVersion;
        source.valid = true;
        stats_.sourceSyncMode = syncMode_;

        if (syncMode_ == GpuMaterialSourceSyncMode::Full) {
            ReleaseSourceReferences(source);
            // A scene layout/residency change does not imply that the
            // material identities changed.  Preserve the existing binding
            // records and slots so a presentation-model add or an animation
            // patch cannot transiently route every surface through the
            // default material.  Resize only the source-record storage; a
            // changed key is reconciled by ResolveAndBindSource below.
            if (source.bindings.size() != sourceRecordCount) {
                source.bindings.resize(sourceRecordCount);
            }
            if (source.slotTable.size() != sourceRecordCount) {
                source.slotTable.resize(
                    sourceRecordCount,
                    kInvalidGpuMaterialIndex);
            }
            if (bindingStorageChanged || layoutChanged) {
                source.bindingVersion = ++bindingVersionCounter_;
            }
            for (SourceBinding& binding : source.bindings) {
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
        if (activeSourceState_ == nullptr ||
            sourceRecordIndex >= activeSourceState_->bindings.size()) {
            return false;
        }

        SourceBinding& binding = activeSourceState_->bindings[sourceRecordIndex];
        const uint32_t materialSlot =
            activeSourceState_->slotTable[sourceRecordIndex];
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
        if (activeSourceState_ == nullptr ||
            sourceRecordIndex >= activeSourceState_->bindings.size()) {
            ++stats_.frameOverflowCount;
            return kInvalidGpuMaterialIndex;
        }

        SourceBinding& binding = activeSourceState_->bindings[sourceRecordIndex];
        const uint32_t oldSlot =
            activeSourceState_->slotTable[sourceRecordIndex] < slots_.size() &&
                slots_[activeSourceState_->slotTable[sourceRecordIndex]].alive
                ? activeSourceState_->slotTable[sourceRecordIndex]
                : kInvalidGpuMaterialIndex;
        const MaterialIdentity identity{
            sourceKey.resource,
            sourceKey.stableMaterialKey
        };
        const uint32_t newSlot = ResolveSlot(identity, data);
        if (newSlot == kInvalidGpuMaterialIndex) {
            if (syncMode_ != GpuMaterialSourceSyncMode::Full) {
                ReleaseSourceReference(oldSlot);
            }
            binding.key = sourceKey;
            binding.active = true;
            binding.finalized = false;
            activeSourceState_->slotTable[sourceRecordIndex] =
                kInvalidGpuMaterialIndex;
            if (oldSlot != kInvalidGpuMaterialIndex) {
                activeSourceState_->bindingVersion =
                    ++bindingVersionCounter_;
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
            activeSourceState_->bindingVersion =
                ++bindingVersionCounter_;
        }
        binding.key = sourceKey;
        activeSourceState_->slotTable[sourceRecordIndex] = newSlot;
        binding.active = true;
        binding.finalized = finalized;
        if (syncMode_ == GpuMaterialSourceSyncMode::Full) {
            MarkSourceSeen(sourceRecordIndex, newSlot);
        }
        return newSlot;
    }

    void GpuMaterialRegistry::EndSourceSync() {
        if (syncMode_ == GpuMaterialSourceSyncMode::Full &&
            activeSourceState_ != nullptr) {
            bool bindingsChanged = false;
            for (size_t sourceIndex = 0;
                sourceIndex < activeSourceState_->bindings.size();
                ++sourceIndex) {

                SourceBinding& binding =
                    activeSourceState_->bindings[sourceIndex];
                if (binding.seenInFullSync) {
                    continue;
                }
                if (activeSourceState_->slotTable[sourceIndex] !=
                    kInvalidGpuMaterialIndex) {
                    bindingsChanged = true;
                }
                binding = {};
                activeSourceState_->slotTable[sourceIndex] =
                    kInvalidGpuMaterialIndex;
            }
            if (bindingsChanged) {
                activeSourceState_->bindingVersion =
                    ++bindingVersionCounter_;
            }
        }

        RetireUnusedSlots();
        syncMode_ = GpuMaterialSourceSyncMode::None;
        RefreshStats();
    }

    bool GpuMaterialRegistry::UpdateProgramBinding(
        uint32_t materialSlot,
        const GpuMaterialProgramBinding& binding) {
        if (materialSlot >= slots_.size() ||
            !slots_[materialSlot].alive) {
            return false;
        }

        Slot& slot = slots_[materialSlot];
        GpuMaterialData& data = slot.data;
        const GpuMaterialProgramBinding current{
            data.materialProgramIndex,
            data.parameterBlockOffset,
            data.resourceTableOffset,
            data.parameterBlockCount,
            data.instanceFlags
        };
        if (current == binding) {
            return true;
        }

        data.materialProgramIndex = binding.materialProgramIndex;
        data.parameterBlockOffset = binding.parameterBlockOffset;
        data.resourceTableOffset = binding.resourceTableOffset;
        data.parameterBlockCount = binding.parameterBlockCount;
        data.instanceFlags = binding.instanceFlags;
        MarkSlotDirty(materialSlot);
        ++dataVersion_;
        ++stats_.frameUpdatedSlotCount;
        RefreshStats();
        return true;
    }

    bool GpuMaterialRegistry::UpdateProgramBlocks(
        uint32_t materialSlot,
        MaterialProgramId programId,
        std::span<const MATH::Vec4> parameters,
        std::span<const uint32_t> resources,
        uint32_t instanceFlags) {
        GpuMaterialProgramBinding binding{};
        return programBlockStore_.Update(
                materialSlot,
                programId,
                parameters,
                resources,
                instanceFlags,
                binding) &&
            UpdateProgramBinding(materialSlot, binding);
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
        return activeSourceState_ != nullptr
            ? std::span<const uint32_t>{
                activeSourceState_->slotTable.data(),
                activeSourceState_->slotTable.size() }
            : std::span<const uint32_t>{};
    }

    std::span<const uint32_t> GpuMaterialRegistry::GetSourceBindings(
        uintptr_t sourceIdentity) const {

        const SourceState* source = FindSourceState(sourceIdentity);
        return source != nullptr
            ? std::span<const uint32_t>{
                source->slotTable.data(),
                source->slotTable.size() }
            : std::span<const uint32_t>{};
    }

    bool GpuMaterialRegistry::TryGetResidentData(
        uint32_t materialSlot,
        GpuMaterialData& outData) const {
        if (materialSlot >= slots_.size() ||
            !slots_[materialSlot].alive) {
            return false;
        }
        outData = slots_[materialSlot].data;
        return true;
    }

    bool GpuMaterialRegistry::StageProgramBlocksFrame(
        uint32_t frameResourceIndex,
        MATH::Vec4* parameterTarget,
        uint32_t* resourceTarget,
        size_t targetMaterialCapacity,
        std::vector<GpuMaterialUploadRange>& outRanges) {
        return programBlockStore_.StageFrame(
            frameResourceIndex,
            parameterTarget,
            resourceTarget,
            targetMaterialCapacity,
            outRanges);
    }

    uint64_t GpuMaterialRegistry::GetBindingVersion() const {
        return activeSourceState_ != nullptr
            ? activeSourceState_->bindingVersion
            : 0u;
    }

    uint64_t GpuMaterialRegistry::GetBindingVersion(
        uintptr_t sourceIdentity) const {

        const SourceState* source = FindSourceState(sourceIdentity);
        return source != nullptr ? source->bindingVersion : 0u;
    }

    void GpuMaterialRegistry::ReleaseSourceBindings(
        uintptr_t sourceIdentity) {

        auto found = sourceStates_.find(sourceIdentity);
        if (found == sourceStates_.end()) {
            return;
        }
        const bool wasActive = activeSourceState_ == &found->second;
        ReleaseSourceReferences(found->second);
        sourceStates_.erase(found);
        ++bindingVersionCounter_;
        if (wasActive) {
            activeSourceState_ = nullptr;
            syncMode_ = GpuMaterialSourceSyncMode::None;
        }
        RetireUnusedSlots();
        RefreshStats();
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
        programBlockStore_.ResetSlot(slotIndex);
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

        if (activeSourceState_ == nullptr ||
            sourceRecordIndex >= activeSourceState_->bindings.size()) {
            return;
        }
        SourceBinding& binding =
            activeSourceState_->bindings[sourceRecordIndex];
        if (syncMode_ == GpuMaterialSourceSyncMode::Full &&
            !binding.seenInFullSync) {
            binding.seenInFullSync = true;
            if (materialSlot < slots_.size() && slots_[materialSlot].alive) {
                ++slots_[materialSlot].sourceReferenceCount;
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

    void GpuMaterialRegistry::ReleaseSourceReferences(SourceState& source) {
        for (size_t sourceIndex = 0;
            sourceIndex < source.bindings.size() &&
            sourceIndex < source.slotTable.size();
            ++sourceIndex) {

            if (source.bindings[sourceIndex].active) {
                ReleaseSourceReference(source.slotTable[sourceIndex]);
            }
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
            programBlockStore_.ResetSlot(slotIndex);
            freeSlots_.push_back(slotIndex);
            if (residentSlotCount_ != 0u) {
                --residentSlotCount_;
            }
            ++stats_.frameRetiredSlotCount;
        }
    }

    void GpuMaterialRegistry::RefreshStats() {
        uint32_t sourceBindingCount = 0u;
        uint32_t pendingSourceResolveCount = 0u;
        for (const auto& [sourceIdentity, source] : sourceStates_) {
            (void)sourceIdentity;
            for (const SourceBinding& binding : source.bindings) {
                if (!binding.active) {
                    continue;
                }
                ++sourceBindingCount;
                if (!binding.finalized) {
                    ++pendingSourceResolveCount;
                }
            }
        }
        stats_.initialized = initialized_;
        stats_.capacity = capacity_;
        stats_.residentSlotCount = residentSlotCount_;
        stats_.sourceBindingCount = sourceBindingCount;
        stats_.pendingSourceResolveCount = pendingSourceResolveCount;
        stats_.pendingFrameSlotCount = pendingFrameSlotCount_;
        stats_.dataVersion = dataVersion_;
        stats_.bindingVersion = bindingVersionCounter_;
    }

    GpuMaterialRegistry::SourceState* GpuMaterialRegistry::FindSourceState(
        uintptr_t sourceIdentity) {

        const auto found = sourceStates_.find(sourceIdentity);
        return found != sourceStates_.end() ? &found->second : nullptr;
    }

    const GpuMaterialRegistry::SourceState*
    GpuMaterialRegistry::FindSourceState(uintptr_t sourceIdentity) const {
        const auto found = sourceStates_.find(sourceIdentity);
        return found != sourceStates_.end() ? &found->second : nullptr;
    }

    void GpuMaterialRegistry::RecreateDefaultSlot() {
        slots_.push_back({});
        Slot& slot = slots_.front();
        programBlockStore_.ResetSlot(0u);
        slot.data = BuildDefaultMaterialData();
        slot.alive = true;
        slot.pinned = true;
        residentSlotCount_ = 1u;
        MarkSlotDirty(0u);
        dataVersion_ = 1u;
        bindingVersionCounter_ = 1u;
    }

} // namespace HIKARI::RENDER3D::MATERIAL
