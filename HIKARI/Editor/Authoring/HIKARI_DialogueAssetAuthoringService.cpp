#include "Editor/Authoring/HIKARI_DialogueAssetAuthoringService.h"

#include <algorithm>
#include <cctype>

#include "Assets/Dialogue/HIKARI_DialogueAsset.h"
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
            return value.empty() ? "Dialogue" : value;
        }

        std::filesystem::path ResolveSourcePath(
            const AssetDatabase& database,
            const AssetRecord& record) {
            return record.sourcePath.is_absolute()
                ? record.sourcePath
                : (database.GetProjectRoot() / record.sourcePath)
                    .lexically_normal();
        }

        DialogueAssetAuthoringResult SaveToRecord(
            AssetDatabase& database,
            const AssetRecord& record,
            const DIALOGUE::DialogueDefinition& definition) {
            DialogueAssetAuthoringResult result{};
            if (record.type != AssetType::Dialogue ||
                !record.guid.IsValid()) {
                result.message = "Selected asset is not a Dialogue";
                return result;
            }
            DialogueAsset asset{};
            asset.guid = record.guid;
            asset.displayName = definition.name;
            asset.definition = definition;
            std::string error{};
            if (!SaveDialogueAsset(
                    ResolveSourcePath(database, record), asset, &error)) {
                result.message = std::move(error);
                return result;
            }
            AssetRecord updated = record;
            updated.displayName = definition.name;
            updated.meta.displayName = definition.name;
            const AssetGuid assetGuid = record.guid;
            const std::filesystem::path sourcePath = record.sourcePath;
            (void)database.WriteMeta(updated);
            if (!database.ScanAssets(false)) {
                result.assetGuid = assetGuid;
                result.sourcePath = sourcePath;
                result.message =
                    "Dialogue saved, but the asset scan failed";
                return result;
            }
            result.success = database.ImportAsset(assetGuid);
            result.assetGuid = assetGuid;
            result.sourcePath = sourcePath;
            result.message = result.success
                ? "Dialogue saved"
                : "Dialogue saved, but import validation failed";
            return result;
        }
    }

    DialogueAssetAuthoringResult CreateDialogueAsset(
        AssetDatabase& database,
        const DIALOGUE::DialogueDefinition& definition) {
        DialogueAssetAuthoringResult result{};
        const std::filesystem::path directory =
            database.GetAssetsRoot() / "Dialogues";
        std::error_code errorCode{};
        std::filesystem::create_directories(directory, errorCode);
        if (errorCode) {
            result.message = "Unable to create Assets/Dialogues";
            return result;
        }
        const std::string baseName = SanitizeFileStem(definition.name);
        std::filesystem::path path = directory / (baseName + ".hdialogue");
        for (uint32_t suffix = 2u; std::filesystem::exists(path); ++suffix) {
            path = directory /
                (baseName + "_" + std::to_string(suffix) + ".hdialogue");
        }
        DialogueAsset asset{};
        asset.displayName = definition.name;
        asset.definition = definition;
        std::string error{};
        if (!SaveDialogueAsset(path, asset, &error)) {
            result.message = std::move(error);
            return result;
        }
        if (!database.ScanAssets(true)) {
            result.message = "Dialogue was written, but the asset scan failed";
            return result;
        }
        const AssetRecord* record = database.FindByPath(path);
        if (record == nullptr || record->type != AssetType::Dialogue ||
            !record->guid.IsValid()) {
            result.message = "Dialogue asset record was not created";
            return result;
        }
        asset.guid = record->guid;
        if (!SaveDialogueAsset(path, asset, &error)) {
            result.message = std::move(error);
            return result;
        }
        return SaveToRecord(database, *record, definition);
    }

    DialogueAssetAuthoringResult UpdateDialogueAsset(
        AssetDatabase& database,
        const AssetGuid& assetGuid,
        const DIALOGUE::DialogueDefinition& definition) {
        const AssetRecord* record = database.FindByGuid(assetGuid);
        if (record == nullptr) {
            DialogueAssetAuthoringResult result{};
            result.message = "Dialogue asset was not found";
            return result;
        }
        return SaveToRecord(database, *record, definition);
    }

    bool LoadDialogueForAuthoring(
        const AssetDatabase& database,
        const AssetGuid& assetGuid,
        DIALOGUE::DialogueDefinition& outDefinition,
        std::string& outMessage) {
        const AssetRecord* record = database.FindByGuid(assetGuid);
        if (record == nullptr || record->type != AssetType::Dialogue) {
            outMessage = "Selected asset is not a Dialogue";
            return false;
        }
        DialogueAsset asset{};
        if (!LoadDialogueAsset(
                ResolveSourcePath(database, *record),
                record->guid,
                asset,
                &outMessage)) {
            return false;
        }
        outDefinition = std::move(asset.definition);
        outMessage = "Dialogue opened";
        return true;
    }

} // namespace HIKARI::EDITOR
