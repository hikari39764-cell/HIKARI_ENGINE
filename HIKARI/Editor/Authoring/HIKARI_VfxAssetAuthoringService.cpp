#include "Editor/Authoring/HIKARI_VfxAssetAuthoringService.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <system_error>
#include <utility>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Material/Authoring/HIKARI_MaterialAssetAuthoringService.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Assets/Vfx/HIKARI_VfxValidation.h"
#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJson.h"
#include "Editor/Authoring/Internal/HIKARI_VfxAssetAuthoringTransaction.h"

namespace HIKARI::EDITOR {
    namespace {

        std::string SanitizeFileStem(std::string value) {
            for (char& character : value) {
                const unsigned char code =
                    static_cast<unsigned char>(character);
                if (!std::isalnum(code) && character != '-' &&
                    character != '_') {
                    character = '_';
                }
            }
            value.erase(
                std::unique(
                    value.begin(), value.end(),
                    [](char left, char right) {
                        return left == '_' && right == '_';
                    }),
                value.end());
            if (value.empty()) {
                value = "VfxEffect";
            }
            return value;
        }

        std::filesystem::path ResolveSourcePath(
            const AssetDatabase& database,
            const AssetRecord& record) {

            return record.sourcePath.is_absolute()
                ? record.sourcePath
                : (database.GetProjectRoot() / record.sourcePath)
                    .lexically_normal();
        }

        VfxAssetAuthoringResult FailCreatedVfxAsset(
            AssetDatabase& database,
            const INTERNAL::VfxAssetCreationFiles& files,
            std::string message) {

            VfxAssetAuthoringResult result{};
            std::string rollbackMessage{};
            if (!INTERNAL::RollBackCreatedVfxAsset(
                    files,
                    rollbackMessage)) {
                result.message = std::move(message) +
                    "; rollback failed: " + rollbackMessage;
                return result;
            }
            if (!database.ScanAssets(false)) {
                result.message = std::move(message) +
                    "; created files were rolled back, but the asset "
                    "database could not be rescanned";
                return result;
            }
            result.message = std::move(message) +
                "; created source and metadata were rolled back";
            return result;
        }

        bool ValidateForWrite(
            const VFX::VfxAssetData& asset,
            std::string& outMessage) {

            const std::vector<VFX::VfxDiagnostic> diagnostics =
                VFX::ValidateVfxAsset(asset);
            if (!VFX::HasVfxErrors(diagnostics)) {
                return true;
            }
            const auto found = std::find_if(
                diagnostics.begin(), diagnostics.end(),
                [](const VFX::VfxDiagnostic& diagnostic) {
                    return diagnostic.severity ==
                        VFX::VfxDiagnosticSeverity::Error;
                });
            outMessage = found != diagnostics.end()
                ? found->message
                : "VFX asset validation failed";
            if (found != diagnostics.end() && !found->subjectId.empty()) {
                outMessage += " [" + found->subjectId + "]";
            }
            return false;
        }

        VFX::VfxEmitterData* FindEmitter(
            VFX::VfxAssetData& asset,
            std::string_view emitterId) noexcept {

            const auto found = std::find_if(
                asset.emitters.begin(),
                asset.emitters.end(),
                [emitterId](const VFX::VfxEmitterData& emitter) {
                    return emitter.id == emitterId;
                });
            return found == asset.emitters.end() ? nullptr : &*found;
        }

        bool RollBackPrivateMaterials(
            AssetDatabase& database,
            const INTERNAL::VfxAssetAuthoringRoots& roots,
            const std::vector<std::filesystem::path>& sourcePaths,
            std::string& outMessage) {

            std::vector<std::filesystem::path> metaPaths{};
            metaPaths.reserve(sourcePaths.size());
            for (const std::filesystem::path& sourcePath : sourcePaths) {
                metaPaths.push_back(
                    database.GetMetaPathForSource(sourcePath));
            }
            if (!INTERNAL::RollBackCreatedVfxPrivateMaterials(
                    roots, sourcePaths, metaPaths, outMessage)) {
                return false;
            }
            if (!database.ScanAssets(false)) {
                outMessage =
                    "private material files were rolled back, but the asset database could not be rescanned";
                return false;
            }
            return true;
        }

        VfxAssetAuthoringResult SaveToRecord(
            AssetDatabase& database,
            const AssetRecord& record,
            const VFX::VfxAssetData& asset) {

            VfxAssetAuthoringResult result{};
            if (record.type != AssetType::VfxEffect ||
                !record.guid.IsValid()) {
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
            if (!INTERNAL::ResolveVfxAssetAuthoringRoots(
                    database.GetProjectRoot(),
                    database.GetAssetsRoot(),
                    database.GetSourceMetaRoot(),
                    roots,
                    result.message)) {
                return result;
            }
            std::filesystem::path resolvedSourcePath{};
            if (!INTERNAL::ResolveVfxSourceWriteTarget(
                    roots,
                    ResolveSourcePath(database, record),
                    resolvedSourcePath,
                    result.message)) {
                return result;
            }
            std::string error{};
            if (!VFX::SERIALIZATION::SaveVfxSourceJson(
                    resolvedSourcePath,
                    asset,
                    error)) {
                result.message = std::move(error);
                return result;
            }

            AssetRecord updated = record;
            updated.displayName = asset.name;
            updated.meta.displayName = asset.name;
            if (!database.WriteMeta(updated)) {
                result.message =
                    "VFX source was saved, but metadata update failed; "
                    "the existing source file was not deleted";
                return result;
            }
            if (!database.ScanAssets(false)) {
                result.message =
                    "VFX source and metadata were saved, but the asset "
                    "database rescan failed; existing files were not deleted";
                return result;
            }
            const AssetRecord* refreshed = database.FindByGuid(guid);
            if (refreshed == nullptr ||
                refreshed->type != AssetType::VfxEffect) {
                result.message =
                    "VFX source and metadata were saved, but the asset "
                    "record could not be restored; existing files were "
                    "not deleted";
                return result;
            }
            result.success = database.ImportAsset(guid);
            result.assetGuid = guid;
            result.sourcePath = sourcePath;
            result.message = result.success
                ? "VFX asset updated"
                : "VFX source saved, but import validation failed";
            return result;
        }

    } // namespace

    bool BuildVfxPresetMaterialSource(
        const VFX::PRESETS::VfxPresetMaterialRecipe& recipe,
        MaterialAssetData& outMaterial,
        std::string& outMessage) {

        if (!recipe.textureAssetId.IsValid()) {
            outMessage = "VFX preset material texture ID is empty";
            return false;
        }
        MaterialAssetData material =
            ASSETS::MATERIAL::CreateMaterialFromTemplate(
                MaterialTemplateId::ParticleUnlitTextured,
                recipe.displayName);
        const auto resource = std::find_if(
            material.graph.resources.begin(),
            material.graph.resources.end(),
            [](const MaterialGraphResourceData& value) {
                return value.id == "resource.particleTexture";
            });
        if (resource == material.graph.resources.end()) {
            outMessage =
                "Textured Particle Unlit template has no particle texture binding";
            return false;
        }
        resource->assetGuid = recipe.textureAssetId;
        resource->enabled = true;
        outMaterial = std::move(material);
        outMessage.clear();
        return true;
    }

    VfxAssetAuthoringResult CreateVfxAsset(
        AssetDatabase& database,
        const VFX::VfxAssetData& asset) {

        VfxAssetAuthoringResult result{};
        if (!ValidateForWrite(asset, result.message)) {
            return result;
        }

        INTERNAL::VfxAssetAuthoringRoots roots{};
        if (!INTERNAL::ResolveVfxAssetAuthoringRoots(
                database.GetProjectRoot(),
                database.GetAssetsRoot(),
                database.GetSourceMetaRoot(),
                roots,
                result.message)) {
            return result;
        }
        const std::filesystem::path directory = roots.assetsRoot / "Vfx";
        std::error_code directoryError{};
        std::filesystem::create_directories(directory, directoryError);
        if (directoryError) {
            result.message = "Failed to create VFX asset directory: " +
                directoryError.message();
            return result;
        }

        const std::string baseName = SanitizeFileStem(asset.name);
        std::filesystem::path path = directory / (baseName + ".hvfx");
        std::error_code existsError{};
        for (uint32_t suffix = 2u;; ++suffix) {
            const bool sourceExists =
                std::filesystem::exists(path, existsError);
            if (existsError) {
                result.message =
                    "Failed to inspect the VFX asset path: " +
                    existsError.message();
                return result;
            }
            const bool metaExists = std::filesystem::exists(
                database.GetMetaPathForSource(path),
                existsError);
            if (existsError) {
                result.message =
                    "Failed to inspect the VFX metadata path: " +
                    existsError.message();
                return result;
            }
            if (!sourceExists && !metaExists) {
                break;
            }
            path = directory /
                (baseName + "_" + std::to_string(suffix) + ".hvfx");
        }

        INTERNAL::VfxAssetCreationFiles creationFiles{};
        if (!INTERNAL::ResolveVfxAssetCreationFiles(
                roots,
                path,
                database.GetMetaPathForSource(path),
                creationFiles,
                result.message)) {
            return result;
        }
        if (creationFiles.sourceExistedBefore ||
            creationFiles.metaExistedBefore) {
            result.message =
                "VFX creation source or metadata target appeared before "
                "it could be written";
            return result;
        }

        std::string error{};
        if (!VFX::SERIALIZATION::SaveVfxSourceJson(
                creationFiles.sourcePath,
                asset,
                error)) {
            result.message = std::move(error);
            return result;
        }
        if (!database.ScanAssets(true)) {
            return FailCreatedVfxAsset(
                database,
                creationFiles,
                "VFX source was written, but the asset scan failed");
        }
        const AssetRecord* record = database.FindByPath(
            creationFiles.sourcePath);
        if (record == nullptr || record->type != AssetType::VfxEffect ||
            !record->guid.IsValid()) {
            return FailCreatedVfxAsset(
                database,
                creationFiles,
                "VFX source was written, but no valid VFX asset record "
                "was created");
        }

        const AssetGuid guid = record->guid;
        const std::filesystem::path sourcePath = record->sourcePath;
        AssetRecord updated = *record;
        updated.displayName = asset.name;
        updated.meta.displayName = asset.name;
        if (!database.WriteMeta(updated)) {
            return FailCreatedVfxAsset(
                database,
                creationFiles,
                "VFX source was written, but its metadata could not be "
                "updated");
        }
        if (!database.ScanAssets(false)) {
            return FailCreatedVfxAsset(
                database,
                creationFiles,
                "VFX source and metadata were written, but the asset "
                "database rescan failed");
        }
        const AssetRecord* refreshed = database.FindByGuid(guid);
        if (refreshed == nullptr ||
            refreshed->type != AssetType::VfxEffect ||
            refreshed->sourcePath != sourcePath) {
            return FailCreatedVfxAsset(
                database,
                creationFiles,
                "VFX source and metadata were written, but the asset "
                "record was not stable after rescan");
        }
        result.success = database.ImportAsset(guid);
        result.assetGuid = guid;
        result.sourcePath = sourcePath;
        result.message = result.success
            ? "VFX asset created"
            : "VFX asset created, but import validation failed";
        return result;
    }

    VfxAssetAuthoringResult CreateVfxPresetAsset(
        AssetDatabase& database,
        const VFX::VfxAssetData& asset,
        const std::vector<VFX::PRESETS::VfxPresetMaterialRecipe>&
            privateMaterials) {

        VfxAssetAuthoringResult result{};
        if (!ValidateForWrite(asset, result.message)) {
            return result;
        }
        if (privateMaterials.empty()) {
            return CreateVfxAsset(database, asset);
        }

        INTERNAL::VfxAssetAuthoringRoots roots{};
        if (!INTERNAL::ResolveVfxAssetAuthoringRoots(
                database.GetProjectRoot(),
                database.GetAssetsRoot(),
                database.GetSourceMetaRoot(),
                roots,
                result.message)) {
            return result;
        }

        VFX::VfxAssetData authoredAsset = asset;
        const std::filesystem::path materialDirectory =
            roots.assetsRoot / "Vfx" /
            (SanitizeFileStem(asset.name) + "_Private") / "Materials";
        ASSETS::MATERIAL::MaterialAssetAuthoringService materialService(
            database);
        std::vector<std::filesystem::path> createdMaterialSources{};
        createdMaterialSources.reserve(privateMaterials.size());
        std::vector<std::string> materializedEmitters{};
        materializedEmitters.reserve(privateMaterials.size());

        for (const VFX::PRESETS::VfxPresetMaterialRecipe& recipe :
             privateMaterials) {
            if (std::find(
                    materializedEmitters.begin(),
                    materializedEmitters.end(),
                    recipe.emitterId) != materializedEmitters.end()) {
                result.message =
                    "VFX preset contains duplicate private material recipes for emitter " +
                    recipe.emitterId;
                std::string rollbackMessage{};
                if (!RollBackPrivateMaterials(
                        database,
                        roots,
                        createdMaterialSources,
                        rollbackMessage)) {
                    result.message += "; rollback failed: " + rollbackMessage;
                }
                return result;
            }
            VFX::VfxEmitterData* emitter =
                FindEmitter(authoredAsset, recipe.emitterId);
            if (emitter == nullptr ||
                emitter->renderCommon.materialAssetGuid.IsValid()) {
                continue;
            }

            MaterialAssetData material{};
            if (!BuildVfxPresetMaterialSource(
                    recipe, material, result.message)) {
                std::string rollbackMessage{};
                if (!RollBackPrivateMaterials(
                        database,
                        roots,
                        createdMaterialSources,
                        rollbackMessage)) {
                    result.message += "; rollback failed: " + rollbackMessage;
                }
                return result;
            }

            const ASSETS::MATERIAL::MaterialAuthoringResult created =
                materialService.Create(material, materialDirectory);
            if (!created.sourcePath.empty()) {
                createdMaterialSources.push_back(
                    created.sourcePath.is_absolute()
                        ? created.sourcePath.lexically_normal()
                        : (database.GetProjectRoot() / created.sourcePath)
                            .lexically_normal());
            }
            if (!created.success || !created.assetGuid.IsValid()) {
                result.message =
                    "Failed to create VFX private material: " +
                    created.message;
                std::string rollbackMessage{};
                if (!RollBackPrivateMaterials(
                        database,
                        roots,
                        createdMaterialSources,
                        rollbackMessage)) {
                    result.message += "; rollback failed: " + rollbackMessage;
                }
                return result;
            }
            emitter->renderCommon.materialAssetGuid = created.assetGuid;
            materializedEmitters.push_back(recipe.emitterId);
        }

        result = CreateVfxAsset(database, authoredAsset);
        if (result.assetGuid.IsValid()) {
            result.authoredAsset = std::move(authoredAsset);
            if (result.success) {
                result.message =
                    "VFX preset and private particle materials created";
            }
            return result;
        }

        const std::string createMessage = result.message;
        std::string rollbackMessage{};
        if (!RollBackPrivateMaterials(
                database,
                roots,
                createdMaterialSources,
                rollbackMessage)) {
            result.message = createMessage +
                "; private material rollback failed: " + rollbackMessage;
        } else {
            result.message = createMessage +
                "; private materials were rolled back";
        }
        return result;
    }

    VfxAssetAuthoringResult UpdateVfxAsset(
        AssetDatabase& database,
        const AssetGuid& assetGuid,
        const VFX::VfxAssetData& asset) {

        const AssetRecord* record = database.FindByGuid(assetGuid);
        if (record == nullptr) {
            VfxAssetAuthoringResult result{};
            result.message = "Selected VFX asset was not found";
            return result;
        }
        return SaveToRecord(database, *record, asset);
    }

    bool LoadVfxForAuthoring(
        const AssetDatabase& database,
        const AssetGuid& assetGuid,
        VFX::VfxAssetData& outAsset,
        std::string& outMessage) {

        const AssetRecord* record = database.FindByGuid(assetGuid);
        if (record == nullptr || record->type != AssetType::VfxEffect) {
            outMessage = "Selected asset is not a VFX asset";
            return false;
        }
        if (!VFX::SERIALIZATION::LoadVfxSourceJson(
                ResolveSourcePath(database, *record),
                outAsset,
                outMessage)) {
            return false;
        }
        outMessage = "VFX asset loaded for authoring";
        return true;
    }

} // namespace HIKARI::EDITOR
