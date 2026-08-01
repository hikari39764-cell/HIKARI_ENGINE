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

    // Applies only stable runtime indices/offsets. Parameter values and
    // resource descriptors remain in their independent block buffers and do
    // not participate in a MaterialProgramKey or PSO key.
    inline bool ApplyGpuMaterialProgramBinding(
        GpuMaterialData& data,
        const Material& material,
        GpuMaterialProgramBlockLocation location) noexcept {
        const MaterialProgramId programId = material.GetMaterialProgramId();
        const size_t parameterCount =
            material.GetMaterialProgramParameterBlock().size();
        if (!programId ||
            parameterCount >
                static_cast<size_t>((std::numeric_limits<uint32_t>::max)())) {
            return false;
        }

        data.materialProgramIndex = programId.value;
        data.parameterBlockOffset = location.parameterBlockOffset;
        data.resourceTableOffset = location.resourceTableOffset;
        data.parameterBlockCount = static_cast<uint32_t>(parameterCount);
        data.instanceFlags = material.GetMaterialProgramInstanceFlags();
        return true;
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
            material.GetMaterialProgramInstanceFlags()
        };
        return true;
    }

} // namespace HIKARI::RENDER3D::MATERIAL
