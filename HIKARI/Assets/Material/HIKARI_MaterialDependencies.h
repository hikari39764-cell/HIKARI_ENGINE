#pragma once

#include <string>
#include <functional>
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

    // 編集用パッケージは無効なスロットも保持する。参照の列挙と再配置を共有する。
    void VisitMaterialAssetReferences(
        MaterialAssetData& data,
        const std::function<void(AssetGuid&, MaterialDependencyKind, std::string)>& visitor,
        bool includeInactive = false);

} // namespace HIKARI
