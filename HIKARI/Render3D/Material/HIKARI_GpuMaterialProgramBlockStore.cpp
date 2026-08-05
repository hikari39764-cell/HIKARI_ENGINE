#include "Render3D/Material/HIKARI_GpuMaterialProgramBlockStore.h"

#include <algorithm>
#include <cstring>

namespace HIKARI::RENDER3D::MATERIAL {

    namespace {
        GpuMaterialProgramParameterBlock BuildNeutralParameters() {
            GpuMaterialProgramParameterBlock parameters{};
            parameters.fill({ 0.0f, 0.0f, 0.0f, 0.0f });
            parameters[22] = { 0.50f, 0.50f, 0.48f, 0.08f };
            parameters[23] = { 0.0f, 1.0f, 0.02f, 0.04f };
            return parameters;
        }

        GpuMaterialProgramResourceBlock BuildNeutralResources() {
            GpuMaterialProgramResourceBlock resources{};
            resources.fill(kInvalidTextureDescriptorIndex);
            return resources;
        }
    }

    bool GpuMaterialProgramBlockStore::Initialize(
        uint32_t materialCapacity,
        uint32_t frameResourceCount) {
        if (materialCapacity == 0u ||
            frameResourceCount == 0u ||
            frameResourceCount >= 32u) {
            return false;
        }
        if (initialized_) {
            return slots_.size() == materialCapacity &&
                frameResourceCount_ == frameResourceCount;
        }
        slots_.resize(materialCapacity);
        frameResourceCount_ = frameResourceCount;
        pendingSlotsByFrame_.resize(frameResourceCount_);
        initialized_ = true;
        Clear();
        return true;
    }

    void GpuMaterialProgramBlockStore::Clear() {
        for (Slot& slot : slots_) {
            slot = {};
            slot.parameters = BuildNeutralParameters();
            slot.resources = BuildNeutralResources();
        }
        pendingSlotsByFrame_.assign(frameResourceCount_, {});
    }

    void GpuMaterialProgramBlockStore::ResetSlot(uint32_t materialSlot) {
        if (materialSlot >= slots_.size()) {
            return;
        }
        ClearPendingMask(materialSlot);
        slots_[materialSlot] = {};
        slots_[materialSlot].parameters = BuildNeutralParameters();
        slots_[materialSlot].resources = BuildNeutralResources();
    }

    bool GpuMaterialProgramBlockStore::Update(
        uint32_t materialSlot,
        MaterialProgramId programId,
        std::span<const MATH::Vec4> parameters,
        std::span<const uint32_t> resources,
        uint32_t instanceFlags,
        GpuMaterialProgramBinding& outBinding) {
        outBinding = {};
        outBinding.instanceFlags = instanceFlags;
        if (!initialized_ || materialSlot >= slots_.size()) {
            return false;
        }
        if (!programId.IsValid() ||
            programId == kStandardPbrMaterialProgramId) {
            ResetSlot(materialSlot);
            return true;
        }
        if (parameters.size() > kGpuMaterialProgramParameterSlotCount ||
            resources.size() > kGpuMaterialProgramResourceSlotCount) {
            return false;
        }

        GpuMaterialProgramParameterBlock packedParameters =
            BuildNeutralParameters();
        std::copy(parameters.begin(), parameters.end(), packedParameters.begin());
        GpuMaterialProgramResourceBlock packedResources =
            BuildNeutralResources();
        std::copy(resources.begin(), resources.end(), packedResources.begin());

        Slot& slot = slots_[materialSlot];
        const bool changed = !slot.active ||
            std::memcmp(
                slot.parameters.data(),
                packedParameters.data(),
                sizeof(packedParameters)) != 0 ||
            std::memcmp(
                slot.resources.data(),
                packedResources.data(),
                sizeof(packedResources)) != 0;
        slot.parameters = packedParameters;
        slot.resources = packedResources;
        slot.active = true;
        if (changed) {
            MarkDirty(materialSlot);
        }

        outBinding.materialProgramIndex = programId.value;
        outBinding.parameterBlockOffset =
            materialSlot * kGpuMaterialProgramParameterSlotCount;
        outBinding.resourceTableOffset =
            materialSlot * kGpuMaterialProgramResourceSlotCount;
        outBinding.parameterBlockCount =
            static_cast<uint32_t>(parameters.size());
        outBinding.instanceFlags = instanceFlags |
            GPU_MATERIAL_PROGRAM_FLAGS::UsesProgramBlock;
        return true;
    }

    bool GpuMaterialProgramBlockStore::StageFrame(
        uint32_t frameResourceIndex,
        MATH::Vec4* parameterTarget,
        uint32_t* resourceTarget,
        size_t targetMaterialCapacity,
        std::vector<GpuMaterialUploadRange>& outRanges) {
        outRanges.clear();
        if (!initialized_ ||
            parameterTarget == nullptr ||
            resourceTarget == nullptr ||
            frameResourceIndex >= pendingSlotsByFrame_.size()) {
            return false;
        }

        std::vector<uint32_t>& pending =
            pendingSlotsByFrame_[frameResourceIndex];
        std::sort(pending.begin(), pending.end());
        pending.erase(std::unique(pending.begin(), pending.end()), pending.end());
        std::vector<uint32_t> remaining{};
        const uint32_t frameBit = 1u << frameResourceIndex;
        for (const uint32_t materialSlot : pending) {
            if (materialSlot >= slots_.size()) {
                continue;
            }
            Slot& slot = slots_[materialSlot];
            if (!slot.active) {
                slot.pendingFrameMask &= ~frameBit;
                continue;
            }
            if (materialSlot >= targetMaterialCapacity) {
                remaining.push_back(materialSlot);
                continue;
            }

            std::copy(
                slot.parameters.begin(),
                slot.parameters.end(),
                parameterTarget +
                    materialSlot * kGpuMaterialProgramParameterSlotCount);
            std::copy(
                slot.resources.begin(),
                slot.resources.end(),
                resourceTarget +
                    materialSlot * kGpuMaterialProgramResourceSlotCount);
            slot.pendingFrameMask &= ~frameBit;
            if (outRanges.empty() ||
                outRanges.back().firstSlot + outRanges.back().slotCount !=
                    materialSlot) {
                outRanges.push_back({ materialSlot, 1u });
            } else {
                ++outRanges.back().slotCount;
            }
        }
        pending = std::move(remaining);
        return true;
    }

    void GpuMaterialProgramBlockStore::MarkDirty(uint32_t materialSlot) {
        Slot& slot = slots_[materialSlot];
        for (uint32_t frameIndex = 0u;
            frameIndex < frameResourceCount_;
            ++frameIndex) {
            const uint32_t frameBit = 1u << frameIndex;
            if ((slot.pendingFrameMask & frameBit) != 0u) {
                continue;
            }
            slot.pendingFrameMask |= frameBit;
            pendingSlotsByFrame_[frameIndex].push_back(materialSlot);
        }
    }

    void GpuMaterialProgramBlockStore::ClearPendingMask(
        uint32_t materialSlot) {
        slots_[materialSlot].pendingFrameMask = 0u;
    }

} // namespace HIKARI::RENDER3D::MATERIAL
