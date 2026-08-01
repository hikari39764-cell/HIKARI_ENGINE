#pragma once

#include <string_view>

#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Render3D/Material/Program/HIKARI_MaterialProgramRegistry.h"

namespace HIKARI {

    class AssetRegistry;
    class Material;

    class MaterialRuntimeBuilder {
    public:
        bool BuildRuntimeMaterial(
            const PbrMaterialAssetData& data,
            const AssetRegistry& assetRegistry,
            Material& outMaterial,
            std::string_view debugName) const;

        bool BuildRuntimeMaterial(
            const MaterialAssetData& data,
            const AssetRegistry& assetRegistry,
            RENDER3D::MATERIAL::MaterialProgramRegistry& programRegistry,
            Material& outMaterial,
            std::string_view debugName,
            RENDER3D::MATERIAL::MaterialProgramResolution* outResolution = nullptr) const;
    };

} // namespace HIKARI
