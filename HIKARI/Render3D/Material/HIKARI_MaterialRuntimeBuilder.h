#pragma once

#include <string_view>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

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
    };

} // namespace HIKARI
