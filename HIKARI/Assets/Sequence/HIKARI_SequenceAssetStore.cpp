#include "Assets/Sequence/HIKARI_SequenceAssetStore.h"

#include "Assets/HIKARI_AssetDatabase.h"

namespace HIKARI {

    void SequenceAssetStore::SetAssetDatabase(
        const AssetDatabase* assetDatabase) noexcept {

        if (assetDatabase_ != assetDatabase) {
            cache_.clear();
        }
        assetDatabase_ = assetDatabase;
    }

    std::shared_ptr<const SequenceAsset> SequenceAssetStore::Load(
        const AssetGuid& guid,
        std::string* outError) {

        if (assetDatabase_ == nullptr || !guid.IsValid()) {
            if (outError != nullptr) {
                *outError = "Sequence asset store is not configured";
            }
            return {};
        }
        const AssetRecord* record = assetDatabase_->FindByGuid(guid);
        if (record == nullptr || record->type != AssetType::Sequence) {
            if (outError != nullptr) {
                *outError = "Sequence asset was not found";
            }
            return {};
        }
        const std::filesystem::path path = record->sourcePath.is_absolute()
            ? record->sourcePath
            : (assetDatabase_->GetProjectRoot() / record->sourcePath)
                .lexically_normal();
        std::error_code ec{};
        const std::filesystem::file_time_type lastWrite =
            std::filesystem::last_write_time(path, ec);
        const auto cached = cache_.find(guid.value);
        if (!ec && cached != cache_.end() && cached->second.asset &&
            cached->second.lastWriteTime == lastWrite) {
            if (outError != nullptr) {
                outError->clear();
            }
            return cached->second.asset;
        }

        auto loaded = std::make_shared<SequenceAsset>();
        std::string error{};
        if (!LoadSequenceAsset(path, guid, *loaded, &error)) {
            if (outError != nullptr) {
                *outError = std::move(error);
            }
            return {};
        }
        cache_[guid.value] = { loaded, lastWrite };
        if (outError != nullptr) {
            outError->clear();
        }
        return loaded;
    }

    void SequenceAssetStore::Invalidate(const AssetGuid& guid) {
        cache_.erase(guid.value);
    }

    void SequenceAssetStore::Clear() {
        cache_.clear();
    }

} // namespace HIKARI
