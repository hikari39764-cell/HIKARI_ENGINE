#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "Assets/HIKARI_AssetFileCache.h"
#include "Assets/Sprite/HIKARI_SpriteAsset.h"
#include "Assets/Sprite/HIKARI_SpriteAssetSerialization.h"

namespace HIKARI::SPRITE {

class SpriteAssetStore {
public:
    void Configure(const AssetDatabase* database);
    std::shared_ptr<const SpriteAssetData> Load(const AssetGuid& guid, std::string& outError) const;

private:
    const AssetDatabase* database_ = nullptr;
    AssetFileCache<SpriteAssetData, AssetType::Sprite, &LoadSpriteAssetData> cache_{};
};

} // namespace HIKARI::SPRITE
