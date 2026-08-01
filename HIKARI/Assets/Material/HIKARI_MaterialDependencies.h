#pragma once

#include <string>
#include <vector>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI {

    enum class MaterialDependencyKind : uint8_t {
        Texture,
        ParentMaterial,
        Function,
        Profile,
        Ramp,
    };

    struct MaterialAssetDependency {
        AssetGuid assetGuid{};
        MaterialDependencyKind kind = MaterialDependencyKind::Texture;
        std::string role{};
    };

    std::vector<MaterialAssetDependency> CollectMaterialAssetDependencies(
        const MaterialAssetData& data);

} // namespace HIKARI
