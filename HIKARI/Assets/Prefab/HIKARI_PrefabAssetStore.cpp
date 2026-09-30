#include "Assets/Prefab/HIKARI_PrefabAssetStore.h"

#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Scene/Prefab/HIKARI_PrefabSerializer.h"

namespace HIKARI {

bool LoadPrefabDocumentAsset(const std::filesystem::path& path, PrefabDocument& outDocument, std::string& outError) {
    return PrefabSerializer{}.LoadFromFile(path, outDocument, outError);
}

void PrefabAssetStore::Configure(const AssetDatabase* assetDatabase) {
    assetDatabase_ = assetDatabase;
    cache_.Configure(assetDatabase);
}

std::shared_ptr<const PrefabDocument> PrefabAssetStore::Load(const AssetGuid& guid, std::string& outError,
                                                             PrefabAssetLoadFailure* outFailure) const {
    if (outFailure != nullptr) {
        *outFailure = PrefabAssetLoadFailure::None;
    }
    if (assetDatabase_ == nullptr) {
        outError = "Prefab asset database is not bound.";
        if (outFailure != nullptr) {
            *outFailure = PrefabAssetLoadFailure::Dependency;
        }
        return {};
    }
    const AssetRecord* record = assetDatabase_->FindByGuid(guid);
    if (record == nullptr || record->type != AssetType::Prefab) {
        outError = "Prefab asset GUID is missing or belongs to a different asset type.";
        if (outFailure != nullptr) {
            *outFailure = PrefabAssetLoadFailure::InvalidAsset;
        }
        return {};
    }
    if (!record->lastImportSucceeded) {
        outError = "Prefab asset has not imported successfully: " + record->lastImportMessage;
        if (outFailure != nullptr) {
            *outFailure = PrefabAssetLoadFailure::Dependency;
        }
        return {};
    }
    std::filesystem::path path = record->sourcePath;
    if (assetDatabase_->IsCookedContent()) {
        const std::string artifact =
            ASSETS::SEMANTICS::FindAssetArtifactPath(*record, ASSETS::SEMANTICS::AssetArtifactKind::PrefabRuntime);
        if (artifact.empty()) {
            outError = "Cooked Prefab runtime artifact is missing.";
            if (outFailure != nullptr) {
                *outFailure = PrefabAssetLoadFailure::Dependency;
            }
            return {};
        }
        path = TEXT::PathFromUtf8(artifact);
    }
    std::shared_ptr<const PrefabDocument> prefab = cache_.Load(guid, path, outError);
    if (!prefab && outFailure != nullptr) {
        *outFailure = PrefabAssetLoadFailure::Dependency;
    }
    return prefab;
}

std::shared_ptr<const PrefabDocument> PrefabAssetStore::LoadSourceForAdmission(const AssetGuid& guid,
                                                                               std::string& outError) const {
    if (assetDatabase_ == nullptr || assetDatabase_->IsCookedContent()) {
        outError = "Source Prefab admission requires an authoring asset database.";
        return {};
    }
    const AssetRecord* record = assetDatabase_->FindByGuid(guid);
    if (record == nullptr || record->type != AssetType::Prefab || !record->sourceExists || record->duplicateGuid) {
        outError = "Prefab source record is missing, invalid, or duplicated: " + guid.value;
        return {};
    }
    const std::filesystem::path sourcePath =
        record->sourcePath.is_absolute() ? record->sourcePath
                                         : (assetDatabase_->GetProjectRoot() / record->sourcePath).lexically_normal();
    return cache_.Load(guid, sourcePath, outError, true);
}

} // namespace HIKARI
