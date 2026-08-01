#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"

namespace HIKARI {
    class AssetRegistry;
}

namespace HIKARI::RENDER3D::MATERIAL {

    struct MaterialProgramRuntimeData {
        std::vector<MATH::Vec4> parameters{};
        std::vector<RuntimeTextureSlot> resources{};
        uint32_t instanceFlags = 0u;
    };

    MaterialProgramRuntimeData BuildMaterialProgramRuntimeData(
        const MaterialProgramIR& program,
        MaterialProgramResolutionKind resolutionKind,
        const AssetRegistry& assetRegistry,
        std::string_view debugName);

} // namespace HIKARI::RENDER3D::MATERIAL
