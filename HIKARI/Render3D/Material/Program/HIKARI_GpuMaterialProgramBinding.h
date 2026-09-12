#pragma once

#include <cstdint>
#include <limits>

#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"

namespace HIKARI::RENDER3D::MATERIAL {

    struct GpuMaterialProgramBlockLocation {
        uint32_t parameterBlockOffset = 0u;
        uint32_t resourceTableOffset = 0u;
    };

    inline void ApplyGpuMaterialProgramBinding(GpuMaterialData& data,
        const GpuMaterialProgramBinding& binding) noexcept {
        data.materialProgramIndex = binding.materialProgramIndex;
        data.parameterBlockOffset = binding.parameterBlockOffset;
        data.resourceTableOffset = binding.resourceTableOffset;
        data.parameterBlockCount = binding.parameterBlockCount;
        data.instanceFlags = binding.instanceFlags;
    }

    inline bool BuildGpuMaterialProgramBinding(
        const Material& material,
        GpuMaterialProgramBlockLocation location,
        GpuMaterialProgramBinding& outBinding) noexcept {
        const MaterialProgramId programId = material.GetMaterialProgramId();
        const size_t parameterCount =
            material.GetMaterialProgramParameterBlock().size();
        if (!programId ||
            parameterCount >
                static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
            return false;
        }
        outBinding = GpuMaterialProgramBinding{
            programId.value,
            location.parameterBlockOffset,
            location.resourceTableOffset,
            static_cast<uint32_t>(parameterCount),
            material.GetMaterialProgramInstanceFlags() |
                GPU_MATERIAL_PROGRAM_FLAGS::UsesProgramBlock
        };
        return true;
    }

    // オフセットと UsesProgramBlock の契約は snapshot / runtime で共有する。
    inline bool ApplyGpuMaterialProgramBinding(GpuMaterialData& data, const Material& material,
        GpuMaterialProgramBlockLocation location) noexcept {
        GpuMaterialProgramBinding binding;
        if (!BuildGpuMaterialProgramBinding(material, location, binding)) return false;
        ApplyGpuMaterialProgramBinding(data, binding);
        return true;
    }

} // namespace HIKARI::RENDER3D::MATERIAL
