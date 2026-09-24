#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI {
bool LoadGameplayCameraAsset(const std::filesystem::path& path, CAMERA::GameplayCameraProfile& profile,
                             std::string& error) {
    nlohmann::json root;
    return SERIALIZATION::JSON::ReadJsonFile(path, root, &error) &&
           CAMERA::ReadGameplayCameraProfile(root, profile, error);
}
bool SaveGameplayCameraAsset(const std::filesystem::path& path, const CAMERA::GameplayCameraProfile& profile,
                             std::string& error) {
    return CAMERA::ValidateGameplayCameraProfile(profile, error) &&
           SERIALIZATION::JSON::WriteJsonFile(path, CAMERA::WriteGameplayCameraProfile(profile), &error);
}
void GameplayCameraAssetStore::Configure(const AssetDatabase* database) {
    database_ = database;
    revision_ = 0;
    entries_.clear();
}
std::shared_ptr<const CAMERA::GameplayCameraProfile> GameplayCameraAssetStore::Load(const AssetGuid& guid,
                                                                                    std::string& error) const {
    if (!database_ || !guid.IsValid()) {
        error = "No gameplay camera asset is bound.";
        return {};
    }
    // アセット更新時だけ無効化する。毎フレームのファイル探索や JSON 読み込みを避ける。
    if (revision_ != database_->GetContentRevision()) {
        entries_.clear();
        revision_ = database_->GetContentRevision();
    }
    if (const auto it = entries_.find(guid.value); it != entries_.end()) {
        error = it->second.error;
        return it->second.profile;
    }
    Entry entry;
    const auto* record = database_->FindByGuid(guid);
    if (!record || record->type != AssetType::GameplayCamera)
        entry.error = "Gameplay camera asset not found: " + guid.value;
    else {
        auto profile = std::make_shared<CAMERA::GameplayCameraProfile>();
        const auto path =
            record->sourcePath.is_absolute() ? record->sourcePath : database_->GetProjectRoot() / record->sourcePath;
        if (LoadGameplayCameraAsset(path, *profile, entry.error))
            entry.profile = std::move(profile);
    }
    error = entry.error;
    auto profile = entry.profile;
    entries_.emplace(guid.value, std::move(entry));
    return profile;
}
} // namespace HIKARI
