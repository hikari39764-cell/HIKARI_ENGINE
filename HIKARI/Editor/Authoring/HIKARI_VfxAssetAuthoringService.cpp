#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Editor/Authoring/HIKARI_VfxAssetAuthoringService.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <system_error>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Vfx/HIKARI_VfxValidation.h"
#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJson.h"
#include "Editor/Authoring/Internal/HIKARI_VfxAssetAuthoringTransaction.h"

namespace HIKARI::EDITOR {
namespace {

VfxAssetAuthoringResult FailCreatedVfxAsset(AssetDatabase& database, const INTERNAL::VfxAssetCreationFiles& files,
                                            std::string message) {
    VfxAssetAuthoringResult result{};
    std::string rollbackMessage{};
    if (!INTERNAL::RollBackCreatedVfxAsset(files, rollbackMessage)) {
        result.message = std::move(message) + "; rollback failed: " + rollbackMessage;
        return result;
    }
    if (!database.ScanAssets(false)) {
        result.message = std::move(message) + "; created files were rolled back, but the asset "
                                              "database could not be rescanned";
        return result;
    }
    result.message = std::move(message) + "; created source and metadata were rolled back";
    return result;
}

bool ValidateForWrite(const VFX::VfxAssetData& asset, std::string& outMessage) {
    const std::vector<VFX::VfxDiagnostic> diagnostics = VFX::ValidateVfxAsset(asset);
    if (!VFX::HasVfxErrors(diagnostics)) {
        return true;
    }
    const auto found = std::find_if(diagnostics.begin(), diagnostics.end(), [](const VFX::VfxDiagnostic& diagnostic) {
        return diagnostic.severity == VFX::VfxDiagnosticSeverity::Error;
    });
    outMessage = found != diagnostics.end() ? found->message : "VFX asset validation failed";
    if (found != diagnostics.end() && !found->subjectId.empty()) {
        outMessage += " [" + found->subjectId + "]";
    }
    return false;
}

VfxAssetAuthoringResult SaveToRecord(AssetDatabase& database, const AssetRecord& record,
                                     const VFX::VfxAssetData& asset) {
    VfxAssetAuthoringResult result{};
    if (record.type != AssetType::VfxEffect || !record.guid.IsValid()) {
        result.message = "Selected asset is not a VFX asset";
        return result;
    }
    if (!ValidateForWrite(asset, result.message)) {
        return result;
    }

    // ScanAssets rebuilds the database record storage. Copy every
    // value needed by the result before that invalidates record.
    const AssetGuid guid = record.guid;
    const std::filesystem::path sourcePath = record.sourcePath;
    INTERNAL::VfxAssetAuthoringRoots roots{};
    if (!INTERNAL::ResolveVfxAssetAuthoringRoots(database.GetProjectRoot(), database.GetAssetsRoot(),
                                                 database.GetSourceMetaRoot(), roots, result.message)) {
        return result;
    }
    std::filesystem::path resolvedSourcePath{};
    if (!INTERNAL::ResolveVfxSourceWriteTarget(roots, database.ResolveSourcePath(record), resolvedSourcePath,
                                               result.message)) {
        return result;
    }
    std::string error{};
    if (!VFX::SERIALIZATION::SaveVfxSourceJson(resolvedSourcePath, asset, error)) {
        result.message = std::move(error);
        return result;
    }

    // ソースを書いた時点で identity を返し、後段失敗でも文書が再試行できるようにする。
    result.assetGuid = guid;
    result.sourcePath = sourcePath;
    AssetRecord updated = record;
    updated.displayName = asset.name;
    updated.meta.displayName = asset.name;
    if (!database.WriteMeta(updated)) {
        result.message = "VFX source was saved, but metadata update failed; "
                         "the existing source file was not deleted";
        return result;
    }
    if (!database.ScanAssets(false)) {
        result.message = "VFX source and metadata were saved, but the asset "
                         "database rescan failed; existing files were not deleted";
        return result;
    }
    const AssetRecord* refreshed = database.FindByGuid(guid);
    if (refreshed == nullptr || refreshed->type != AssetType::VfxEffect) {
        result.message = "VFX source and metadata were saved, but the asset "
                         "record could not be restored; existing files were "
                         "not deleted";
        return result;
    }
    result.success = database.ImportAsset(guid);
    result.message = result.success ? "VFX asset updated" : "VFX source saved, but import validation failed";
    return result;
}

} // namespace

VfxAssetAuthoringResult CreateVfxAsset(AssetDatabase& database, const VFX::VfxAssetData& asset) {
    VfxAssetAuthoringResult result{};
    if (!ValidateForWrite(asset, result.message)) {
        return result;
    }

    INTERNAL::VfxAssetAuthoringRoots roots{};
    if (!INTERNAL::ResolveVfxAssetAuthoringRoots(database.GetProjectRoot(), database.GetAssetsRoot(),
                                                 database.GetSourceMetaRoot(), roots, result.message)) {
        return result;
    }
    const std::filesystem::path directory = roots.assetsRoot / "Vfx";
    std::error_code directoryError{};
    std::filesystem::create_directories(directory, directoryError);
    if (directoryError) {
        result.message = "Failed to create VFX asset directory: " + directoryError.message();
        return result;
    }

    const std::string baseName = ASSETS::SEMANTICS::MakeAssetFileStem(asset.name, "VfxEffect");
    std::filesystem::path path = directory / (baseName + ".hvfx");
    std::error_code existsError{};
    for (uint32_t suffix = 2u;; ++suffix) {
        const bool sourceExists = std::filesystem::exists(path, existsError);
        if (existsError) {
            result.message = "Failed to inspect the VFX asset path: " + existsError.message();
            return result;
        }
        const bool metaExists = std::filesystem::exists(database.GetMetaPathForSource(path), existsError);
        if (existsError) {
            result.message = "Failed to inspect the VFX metadata path: " + existsError.message();
            return result;
        }
        if (!sourceExists && !metaExists) {
            break;
        }
        path = directory / (baseName + "_" + std::to_string(suffix) + ".hvfx");
    }

    INTERNAL::VfxAssetCreationFiles creationFiles{};
    if (!INTERNAL::ResolveVfxAssetCreationFiles(roots, path, database.GetMetaPathForSource(path), creationFiles,
                                                result.message)) {
        return result;
    }
    if (creationFiles.sourceExistedBefore || creationFiles.metaExistedBefore) {
        result.message = "VFX creation source or metadata target appeared before "
                         "it could be written";
        return result;
    }

    std::string error{};
    if (!VFX::SERIALIZATION::SaveVfxSourceJson(creationFiles.sourcePath, asset, error)) {
        result.message = std::move(error);
        return result;
    }
    if (!database.ScanAssets(true)) {
        return FailCreatedVfxAsset(database, creationFiles, "VFX source was written, but the asset scan failed");
    }
    const AssetRecord* record = database.FindByPath(creationFiles.sourcePath);
    if (record == nullptr || record->type != AssetType::VfxEffect || !record->guid.IsValid()) {
        return FailCreatedVfxAsset(database, creationFiles,
                                   "VFX source was written, but no valid VFX asset record "
                                   "was created");
    }

    const AssetGuid guid = record->guid;
    const std::filesystem::path sourcePath = record->sourcePath;
    AssetRecord updated = *record;
    updated.displayName = asset.name;
    updated.meta.displayName = asset.name;
    if (!database.WriteMeta(updated)) {
        return FailCreatedVfxAsset(database, creationFiles,
                                   "VFX source was written, but its metadata could not be "
                                   "updated");
    }
    if (!database.ScanAssets(false)) {
        return FailCreatedVfxAsset(database, creationFiles,
                                   "VFX source and metadata were written, but the asset "
                                   "database rescan failed");
    }
    const AssetRecord* refreshed = database.FindByGuid(guid);
    if (refreshed == nullptr || refreshed->type != AssetType::VfxEffect || refreshed->sourcePath != sourcePath) {
        return FailCreatedVfxAsset(database, creationFiles,
                                   "VFX source and metadata were written, but the asset "
                                   "record was not stable after rescan");
    }
    result.success = database.ImportAsset(guid);
    result.assetGuid = guid;
    result.sourcePath = sourcePath;
    result.message = result.success ? "VFX asset created" : "VFX asset created, but import validation failed";
    return result;
}

VfxAssetAuthoringResult UpdateVfxAsset(AssetDatabase& database, const AssetGuid& assetGuid,
                                       const VFX::VfxAssetData& asset) {
    const AssetRecord* record = database.FindByGuid(assetGuid);
    if (record == nullptr) {
        VfxAssetAuthoringResult result{};
        result.message = "Selected VFX asset was not found";
        return result;
    }
    return SaveToRecord(database, *record, asset);
}

bool LoadVfxForAuthoring(const AssetDatabase& database, const AssetGuid& assetGuid, VFX::VfxAssetData& outAsset,
                         std::string& outMessage) {
    const AssetRecord* record = database.FindByGuid(assetGuid);
    if (record == nullptr || record->type != AssetType::VfxEffect) {
        outMessage = "Selected asset is not a VFX asset";
        return false;
    }
    if (!VFX::SERIALIZATION::LoadVfxSourceJson(database.ResolveSourcePath(*record), outAsset, outMessage)) {
        return false;
    }
    outMessage = "VFX asset loaded for authoring";
    return true;
}

} // namespace HIKARI::EDITOR
