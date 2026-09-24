#include "Assets/HIKARI_DocumentAssetStores.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Text/HIKARI_TextStyleAsset.h"

namespace HIKARI {
namespace {
// 演出プレビューは更新時刻を監視し、実行時の会話・アニメーションは
// 明示的な無効化を使って毎フレームのファイル参照を避ける。
template <bool WatchSource, class Asset, class Loader>
std::shared_ptr<const Asset> LoadSourceAsset(const AssetDatabase* database, const AssetGuid& guid,
                                             ASSETS::INTERNAL::SourceAssetCache<Asset>& cache, AssetType type,
                                             Loader load, std::string* error) {
    const auto fail = [&](const char* message) -> std::shared_ptr<const Asset> {
        if (error)
            *error = message;
        return {};
    };
    if (!database || !guid.IsValid())
        return fail("Asset store is not configured or identity is invalid");
    const auto cached = cache.find(guid.value);
    if constexpr (!WatchSource) {
        if (cached != cache.end()) {
            if (error)
                error->clear();
            return cached->second.asset;
        }
    }
    const auto* record = database->FindByGuid(guid);
    if (!record || record->type != type)
        return fail("Asset record was not found or has the wrong type");
    const auto path = (database->GetProjectRoot() / record->sourcePath).lexically_normal();
    std::filesystem::file_time_type lastWrite{};
    if constexpr (WatchSource) {
        std::error_code ec;
        lastWrite = std::filesystem::last_write_time(path, ec);
        if (!ec && cached != cache.end() && cached->second.asset && cached->second.lastWriteTime == lastWrite) {
            if (error)
                error->clear();
            return cached->second.asset;
        }
    }
    auto asset = std::make_shared<Asset>();
    if (!load(path, record->guid, *asset, error))
        return {};
    cache[guid.value] = {asset, lastWrite};
    if (error)
        error->clear();
    return asset;
}
} // namespace

void AnimationStateMachineAssetStore::SetAssetDatabase(const AssetDatabase* database) noexcept {
    if (database_ != database)
        cache_.clear();
    database_ = database;
}

std::shared_ptr<const AnimationStateMachineAsset> AnimationStateMachineAssetStore::Load(const AssetGuid& guid,
                                                                                        std::string* outError) {
    return LoadSourceAsset<false>(database_, guid, cache_, AssetType::AnimationStateMachine,
                                  LoadAnimationStateMachineAsset, outError);
}

void AnimationStateMachineAssetStore::Invalidate(const AssetGuid& guid) { cache_.erase(guid.value); }

void AnimationStateMachineAssetStore::Clear() { cache_.clear(); }

void SequenceAssetStore::SetAssetDatabase(const AssetDatabase* assetDatabase) noexcept {
    if (assetDatabase_ != assetDatabase) {
        cache_.clear();
    }
    assetDatabase_ = assetDatabase;
}

std::shared_ptr<const SequenceAsset> SequenceAssetStore::Load(const AssetGuid& guid, std::string* outError) {
    return LoadSourceAsset<true>(assetDatabase_, guid, cache_, AssetType::Sequence, LoadSequenceAsset, outError);
}

void SequenceAssetStore::Invalidate(const AssetGuid& guid) { cache_.erase(guid.value); }

void SequenceAssetStore::Clear() { cache_.clear(); }

void DialogueAssetStore::SetAssetDatabase(const AssetDatabase* database) noexcept {
    if (database_ != database) {
        cache_.clear();
        projectStyle_.reset();
    }
    database_ = database;
}

std::shared_ptr<const DialogueAsset> DialogueAssetStore::Load(const AssetGuid& guid, std::string* outError) {
    return LoadSourceAsset<false>(database_, guid, cache_, AssetType::Dialogue, LoadDialogueAsset, outError);
}

std::shared_ptr<const DIALOGUE::DialogueProjectStyle> DialogueAssetStore::LoadProjectStyle(std::string* outError) {
    if (database_ == nullptr) {
        if (outError != nullptr) {
            *outError = "Dialogue project style store is not ready";
        }
        return {};
    }
    if (projectStyle_) {
        if (outError != nullptr)
            outError->clear();
        return projectStyle_;
    }
    auto loaded = std::make_shared<DIALOGUE::DialogueProjectStyle>();
    if (!DIALOGUE::LoadDialogueProjectStyle(database_->GetProjectRoot(), *loaded, outError)) {
        return {};
    }
    DIALOGUE::ResolveDialogueProjectStyleAssets(*loaded, *database_);
    TextAssetStore texts;
    texts.Configure(database_);
    for (const auto& guid :
         {loaded->theme.speakerTextStyle, loaded->theme.bodyTextStyle, loaded->theme.choiceTextStyle}) {
        std::string error;
        const auto style = texts.LoadStyle(guid, error);
        if (!style || texts.ResolveFont(style->font).empty()) {
            if (outError)
                *outError = "Dialogue typography is not ready: " +
                            (style ? "font asset is missing or has not imported" : error);
            return {};
        }
    }
    projectStyle_ = loaded;
    return projectStyle_;
}

void DialogueAssetStore::Invalidate(const AssetGuid& guid) { cache_.erase(guid.value); }

void DialogueAssetStore::InvalidateProjectStyle() noexcept { projectStyle_.reset(); }

void DialogueAssetStore::Clear() {
    cache_.clear();
    projectStyle_.reset();
}

} // namespace HIKARI
