#include "Editor/Authoring/HIKARI_CombatActionAssetAuthoringService.h"

#include <algorithm>
#include <cctype>

#include "Assets/HIKARI_AssetDatabase.h"

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
                    [](char lhs, char rhs) {
                        return lhs == '_' && rhs == '_';
                    }),
                value.end());
            return value.empty() ? "CombatAction" : value;
        }

        std::filesystem::path ResolveSourcePath(
            const AssetDatabase& database,
            const AssetRecord& record) {
            return record.sourcePath.is_absolute()
                ? record.sourcePath
                : (database.GetProjectRoot() / record.sourcePath)
                    .lexically_normal();
        }

        CombatActionAssetAuthoringResult SaveToRecord(
            AssetDatabase& database,
            const AssetRecord& record,
            const ACTION::CombatActionDefinition& definition) {
            CombatActionAssetAuthoringResult result{};
            if (record.type != AssetType::CombatAction ||
                !record.guid.IsValid()) {
                result.message = "Selected asset is not a Combat Action";
                return result;
            }
            ACTION::CombatActionAsset asset{};
            asset.guid = record.guid;
            asset.displayName = definition.displayName;
            asset.definition = definition;
            std::string error{};
            if (!ACTION::SaveCombatActionAsset(
                    ResolveSourcePath(database, record), asset, &error)) {
                result.message = std::move(error);
                return result;
            }
            AssetRecord updated = record;
            updated.displayName = definition.displayName;
            updated.meta.displayName = definition.displayName;
            (void)database.WriteMeta(updated);
            (void)database.ScanAssets(false);
            result.success = database.ImportAsset(record.guid);
            result.assetGuid = record.guid;
            result.sourcePath = record.sourcePath;
            result.message = result.success
                ? "Combat Action saved"
                : "Combat Action saved, but import validation failed";
            return result;
        }
    }

    CombatActionAssetAuthoringResult CreateCombatActionAsset(
        AssetDatabase& database,
        const ACTION::CombatActionDefinition& definition) {
        CombatActionAssetAuthoringResult result{};
        const std::filesystem::path directory =
            database.GetAssetsRoot() / "Actions";
        std::error_code errorCode{};
        std::filesystem::create_directories(directory, errorCode);
        if (errorCode) {
            result.message = "Unable to create Assets/Actions";
            return result;
        }
        const std::string baseName = SanitizeFileStem(
            definition.actionId.empty()
                ? definition.displayName
                : definition.actionId);
        std::filesystem::path path = directory / (baseName + ".haction");
        for (uint32_t suffix = 2u; std::filesystem::exists(path); ++suffix) {
            path = directory /
                (baseName + "_" + std::to_string(suffix) + ".haction");
        }
        ACTION::CombatActionAsset asset{};
        asset.displayName = definition.displayName;
        asset.definition = definition;
        std::string error{};
        if (!ACTION::SaveCombatActionAsset(path, asset, &error)) {
            result.message = std::move(error);
            return result;
        }
        if (!database.ScanAssets(true)) {
            result.message =
                "Combat Action was written, but the asset scan failed";
            return result;
        }
        const AssetRecord* record = database.FindByPath(path);
        if (record == nullptr ||
            record->type != AssetType::CombatAction ||
            !record->guid.IsValid()) {
            result.message = "Combat Action asset record was not created";
            return result;
        }
        asset.guid = record->guid;
        if (!ACTION::SaveCombatActionAsset(path, asset, &error)) {
            result.message = std::move(error);
            return result;
        }
        return SaveToRecord(database, *record, definition);
    }

    CombatActionAssetAuthoringResult UpdateCombatActionAsset(
        AssetDatabase& database,
        const AssetGuid& assetGuid,
        const ACTION::CombatActionDefinition& definition) {
        const AssetRecord* record = database.FindByGuid(assetGuid);
        if (record == nullptr) {
            CombatActionAssetAuthoringResult result{};
            result.message = "Combat Action asset was not found";
            return result;
        }
        return SaveToRecord(database, *record, definition);
    }

    bool LoadCombatActionForAuthoring(
        const AssetDatabase& database,
        const AssetGuid& assetGuid,
        ACTION::CombatActionDefinition& outDefinition,
        std::string& outMessage) {
        const AssetRecord* record = database.FindByGuid(assetGuid);
        if (record == nullptr || record->type != AssetType::CombatAction) {
            outMessage = "Selected asset is not a Combat Action";
            return false;
        }
        ACTION::CombatActionAsset asset{};
        if (!ACTION::LoadCombatActionAsset(
                ResolveSourcePath(database, *record),
                record->guid,
                asset,
                &outMessage)) {
            return false;
        }
        outDefinition = std::move(asset.definition);
        outMessage = "Combat Action opened";
        return true;
    }

} // namespace HIKARI::EDITOR
