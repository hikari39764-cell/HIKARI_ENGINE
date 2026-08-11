#pragma once

#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI {

    class AssetDatabase;

namespace EDITOR {

    struct VfxAssetAuthoringResult {
        bool success = false;
        AssetGuid assetGuid{};
        std::filesystem::path sourcePath{};
        std::string message{};
    };

    VfxAssetAuthoringResult CreateVfxAsset(
        AssetDatabase& assetDatabase,
        const VFX::VfxAssetData& asset);
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
