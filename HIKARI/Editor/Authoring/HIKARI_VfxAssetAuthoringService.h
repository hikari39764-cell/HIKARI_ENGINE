#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"

namespace HIKARI {

    class AssetDatabase;

namespace EDITOR {

    struct VfxAssetAuthoringResult {
        bool success = false;
        AssetGuid assetGuid{};
        std::filesystem::path sourcePath{};
        std::optional<VFX::VfxAssetData> authoredAsset{};
        std::string message{};
    };

    VfxAssetAuthoringResult CreateVfxAsset(
        AssetDatabase& assetDatabase,
        const VFX::VfxAssetData& asset);
    VfxAssetAuthoringResult CreateVfxPresetAsset(
        AssetDatabase& assetDatabase,
        const VFX::VfxAssetData& asset,
        const std::vector<VFX::PRESETS::VfxPresetMaterialRecipe>&
            privateMaterials);
    VfxAssetAuthoringResult UpdateVfxAsset(
        AssetDatabase& assetDatabase,
        const AssetGuid& assetGuid,
        const VFX::VfxAssetData& asset);
    bool LoadVfxForAuthoring(
        const AssetDatabase& assetDatabase,
        const AssetGuid& assetGuid,
        VFX::VfxAssetData& outAsset,
        std::string& outMessage);

} // namespace EDITOR
} // namespace HIKARI
