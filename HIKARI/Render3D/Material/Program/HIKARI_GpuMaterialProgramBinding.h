#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

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

    // 可変長 parameter と固定32 resource の詰め方を、焼き込みと描画で共用する。
    inline bool AppendGpuMaterialProgramBlock(
        GpuMaterialData& data,
        GpuMaterialProgramBinding binding,
        std::span<const MATH::Vec4> parameterValues,
        std::span<const uint32_t> resourceValues,
        std::vector<MATH::Vec4>& parameters,
        std::vector<uint32_t>& resources) {
        if (parameterValues.size() > kGpuMaterialProgramParameterSlotCount ||
            resourceValues.size() > kGpuMaterialProgramResourceSlotCount ||
            parameters.size() > kGpuMaterialProgramParameterCapacity - parameterValues.size() ||
            resources.size() > kGpuMaterialProgramResourceCapacity - kGpuMaterialProgramResourceSlotCount) {
            return false;
        }
        binding.parameterBlockOffset = static_cast<uint32_t>(parameters.size());
        binding.resourceTableOffset = static_cast<uint32_t>(resources.size());
        binding.parameterBlockCount = static_cast<uint32_t>(parameterValues.size());
        try {
            if (parameters.size() + parameterValues.size() > parameters.capacity())
                parameters.reserve((std::min)(size_t(kGpuMaterialProgramParameterCapacity),
                    (std::max)(parameters.size() + parameterValues.size(), parameters.capacity() * 2u)));
            if (resources.size() + kGpuMaterialProgramResourceSlotCount > resources.capacity())
                resources.reserve((std::min)(size_t(kGpuMaterialProgramResourceCapacity),
                    (std::max)(resources.size() + kGpuMaterialProgramResourceSlotCount, resources.capacity() * 2u)));
        } catch (...) {
            return false;
        }
        parameters.insert(parameters.end(), parameterValues.begin(), parameterValues.end());
        resources.resize(resources.size() + kGpuMaterialProgramResourceSlotCount, kInvalidTextureDescriptorIndex);
        for (size_t i = 0; i < resourceValues.size(); ++i)
            resources[binding.resourceTableOffset + i] = resourceValues[i];
        ApplyGpuMaterialProgramBinding(data, binding);
        return true;
    }

} // namespace HIKARI::RENDER3D::MATERIAL
