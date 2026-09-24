#pragma once
#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/Camera/HIKARI_GameplayCameraProfile.h"
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace HIKARI {
class AssetDatabase;
bool LoadGameplayCameraAsset(const std::filesystem::path& path, CAMERA::GameplayCameraProfile& profile,
                             std::string& error);
bool SaveGameplayCameraAsset(const std::filesystem::path& path, const CAMERA::GameplayCameraProfile& profile,
                             std::string& error);

class GameplayCameraAssetStore {
public:
    void Configure(const AssetDatabase* database);
    std::shared_ptr<const CAMERA::GameplayCameraProfile> Load(const AssetGuid& guid, std::string& error) const;

private:
    const AssetDatabase* database_ = nullptr;
    mutable uint64_t revision_ = 0;
    struct Entry {
        std::shared_ptr<const CAMERA::GameplayCameraProfile> profile;
        std::string error;
    };
    mutable std::unordered_map<std::string, Entry> entries_;
};
} // namespace HIKARI
