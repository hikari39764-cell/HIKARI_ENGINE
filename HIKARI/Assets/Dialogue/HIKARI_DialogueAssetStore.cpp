#include "Assets/Dialogue/HIKARI_DialogueAssetStore.h"

#include "Assets/HIKARI_AssetDatabase.h"

namespace HIKARI {

    void DialogueAssetStore::SetAssetDatabase(
        const AssetDatabase* database) noexcept {
        if (database_ != database) {
            cache_.clear();
            projectStyle_.reset();
        }
        database_ = database;
    }

    std::shared_ptr<const DialogueAsset> DialogueAssetStore::Load(
        const AssetGuid& guid,
        std::string* outError) {
        if (!guid.IsValid() || database_ == nullptr) {
            if (outError != nullptr) {
                *outError = "Dialogue asset store is not ready";
            }
            return {};
        }
        if (const auto found = cache_.find(guid.value);
            found != cache_.end()) {
            if (outError != nullptr) outError->clear();
            return found->second;
        }
        const AssetRecord* record = database_->FindByGuid(guid);
        if (record == nullptr || record->type != AssetType::Dialogue) {
            if (outError != nullptr) {
                *outError = "Dialogue asset record was not found";
            }
            return {};
        }
        const std::filesystem::path path = record->sourcePath.is_absolute()
            ? record->sourcePath
            : (database_->GetProjectRoot() / record->sourcePath)
                .lexically_normal();
        auto loaded = std::make_shared<DialogueAsset>();
        if (!LoadDialogueAsset(path, record->guid, *loaded, outError)) {
            return {};
        }
        cache_[guid.value] = loaded;
        return loaded;
    }

    std::shared_ptr<const DIALOGUE::DialogueProjectStyle>
        DialogueAssetStore::LoadProjectStyle(std::string* outError) {
        if (database_ == nullptr) {
            if (outError != nullptr) {
                *outError = "Dialogue project style store is not ready";
            }
            return {};
        }
        if (projectStyle_) {
            if (outError != nullptr) outError->clear();
            return projectStyle_;
        }
        auto loaded = std::make_shared<DIALOGUE::DialogueProjectStyle>();
        if (!DIALOGUE::LoadDialogueProjectStyle(
                database_->GetProjectRoot(),
                *loaded,
                outError)) {
            return {};
        }
        DIALOGUE::ResolveDialogueProjectStyleAssets(*loaded, *database_);
        projectStyle_ = loaded;
        return projectStyle_;
    }

    void DialogueAssetStore::Invalidate(const AssetGuid& guid) {
        cache_.erase(guid.value);
    }

    void DialogueAssetStore::InvalidateProjectStyle() noexcept {
        projectStyle_.reset();
    }

    void DialogueAssetStore::Clear() {
        cache_.clear();
        projectStyle_.reset();
    }

} // namespace HIKARI
