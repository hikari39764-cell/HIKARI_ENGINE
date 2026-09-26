#pragma once
#include "Assets/HIKARI_AssetFileCache.h"
#include "Scene/Camera/HIKARI_GameplayCameraProfile.h"
#include <filesystem>

namespace HIKARI {
bool LoadGameplayCameraAsset(const std::filesystem::path& path, CAMERA::GameplayCameraProfile& profile,
                             std::string& error);
bool SaveGameplayCameraAsset(const std::filesystem::path& path, const CAMERA::GameplayCameraProfile& profile,
                             std::string& error);

using GameplayCameraAssetStore =
    AssetFileCache<CAMERA::GameplayCameraProfile, AssetType::GameplayCamera, &LoadGameplayCameraAsset>;
} // namespace HIKARI
