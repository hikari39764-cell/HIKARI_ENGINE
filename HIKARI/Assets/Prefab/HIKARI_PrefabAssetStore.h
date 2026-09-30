#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "Assets/HIKARI_AssetFileCache.h"
#include "Scene/Prefab/HIKARI_PrefabDocument.h"

namespace HIKARI {

bool LoadPrefabDocumentAsset(const std::filesystem::path& path, PrefabDocument& outDocument, std::string& outError);

enum class PrefabAssetLoadFailure : uint8_t {
    None,
    InvalidAsset,
    Dependency,
};

class PrefabAssetStore {
public:
    void Configure(const AssetDatabase* assetDatabase);
    std::shared_ptr<const PrefabDocument> Load(const AssetGuid& guid, std::string& outError,
                                               PrefabAssetLoadFailure* outFailure = nullptr) const;
    std::shared_ptr<const PrefabDocument> LoadSourceForAdmission(const AssetGuid& guid, std::string& outError) const;

private:
    const AssetDatabase* assetDatabase_ = nullptr;
    AssetFileCache<PrefabDocument, AssetType::Prefab, &LoadPrefabDocumentAsset> cache_{};
};

} // namespace HIKARI
