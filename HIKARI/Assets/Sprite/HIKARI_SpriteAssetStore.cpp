#include "Assets/Sprite/HIKARI_SpriteAssetStore.h"

#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI::SPRITE {

void SpriteAssetStore::Configure(const AssetDatabase* database) {
    database_ = database;
    cache_.Configure(database);
}

std::shared_ptr<const SpriteAssetData> SpriteAssetStore::Load(const AssetGuid& guid, std::string& outError) const {
    if (database_ == nullptr || !guid.IsValid()) {
        outError = "Sprite asset database is not bound or its GUID is invalid.";
        return {};
    }
    const AssetRecord* record = database_->FindByGuid(guid);
    if (record == nullptr || record->duplicateGuid || record->type != AssetType::Sprite) {
        outError = "Sprite asset GUID is missing, duplicated, or belongs to a different asset type: " + guid.value;
        return {};
    }
    if (!record->lastImportSucceeded) {
        outError = "Sprite asset has not imported successfully: " + record->lastImportMessage;
        return {};
    }
    const std::string artifact =
        ASSETS::SEMANTICS::FindAssetArtifactPath(*record, ASSETS::SEMANTICS::AssetArtifactKind::SpriteRuntime);
    if (artifact.empty()) {
        outError = "Sprite runtime artifact is missing: " + guid.value;
        return {};
    }

    std::shared_ptr<const SpriteAssetData> asset = cache_.Load(guid, TEXT::PathFromUtf8(artifact), outError);
    if (!asset) {
        return {};
    }
    for (const SpritePage& page : asset->pages) {
        if (page.textureAssetId != MakeSpritePageTextureAssetId(guid.value, page.id) ||
            (!page.normalTextureAssetId.empty() &&
             page.normalTextureAssetId != MakeSpritePageNormalTextureAssetId(guid.value, page.id))) {
            outError = "Sprite runtime artifact source GUID does not match its asset record: " + guid.value;
            return {};
        }
    }
    outError.clear();
    return asset;
}

} // namespace HIKARI::SPRITE
