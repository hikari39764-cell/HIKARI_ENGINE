#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramTypes.h"

namespace HIKARI::RENDER3D::MATERIAL {

class GpuMaterialProgramBlockStore final {
public:
    bool Initialize(uint32_t materialCapacity, uint32_t frameResourceCount);
    void Clear();
    void ResetSlot(uint32_t materialSlot);

    bool Update(uint32_t materialSlot, MaterialProgramId programId, std::span<const MATH::Vec4> parameters,
                std::span<const uint32_t> resources, uint32_t instanceFlags, GpuMaterialProgramBinding& outBinding);

    bool StageFrame(uint32_t frameResourceIndex, MATH::Vec4* parameterTarget, uint32_t* resourceTarget,
                    size_t targetMaterialCapacity, std::vector<GpuMaterialUploadRange>& outRanges);

private:
    struct Slot {
        GpuMaterialProgramParameterBlock parameters{};
        GpuMaterialProgramResourceBlock resources{};
        uint32_t pendingFrameMask = 0u;
        bool active = false;
    };

    void MarkDirty(uint32_t materialSlot);
    void ClearPendingMask(uint32_t materialSlot);

    uint32_t frameResourceCount_ = 0u;
    bool initialized_ = false;
    std::vector<Slot> slots_{};
    std::vector<std::vector<uint32_t>> pendingSlotsByFrame_{};
};

} // namespace HIKARI::RENDER3D::MATERIAL
