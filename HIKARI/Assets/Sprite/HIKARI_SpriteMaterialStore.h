#pragma once

#include <memory>
#include <string>

#include "Assets/HIKARI_AssetFileCache.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/Sprite/HIKARI_SpriteMaterialAsset.h"
#include "Assets/Sprite/HIKARI_SpriteMaterialSerialization.h"

namespace HIKARI::SPRITE {

class SpriteMaterialStore {
public:
    void Configure(const AssetDatabase* database, const AssetRegistry* registry);

    std::shared_ptr<const SpriteMaterialAssetData> Load(const AssetGuid& guid, std::string& outError) const;
    std::shared_ptr<const SpriteMaterialBinding> Resolve(const AssetGuid& guid, std::string& outError) const;

private:
    const AssetDatabase* database_ = nullptr;
    const AssetRegistry* registry_ = nullptr;
    AssetFileCache<SpriteMaterialAssetData, AssetType::SpriteMaterial, &LoadSpriteMaterialAssetData> cache_{};
};

} // namespace HIKARI::SPRITE
