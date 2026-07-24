#include "Editor/Authoring/HIKARI_SequenceAssetAuthoringService.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Sequence/HIKARI_SequenceAsset.h"

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
                    value.begin(),
                    value.end(),
                    [](char lhs, char rhs) {
                        return lhs == '_' && rhs == '_';
                    }),
                value.end());
            if (value.empty()) {
                value = "Sequence";
            }
            return value;
        }

        std::filesystem::path ResolveSourcePath(
            const AssetDatabase& assetDatabase,
            const AssetRecord& record) {

            return record.sourcePath.is_absolute()
                ? record.sourcePath
                : (assetDatabase.GetProjectRoot() / record.sourcePath)
                    .lexically_normal();
        }

        SequenceAssetAuthoringResult SaveToRecord(
            AssetDatabase& assetDatabase,
            const AssetRecord& record,
            const CinematicSequence& sequence) {

            SequenceAssetAuthoringResult result{};
            if (record.type != AssetType::Sequence ||
                !record.guid.IsValid()) {
                result.message = "Selected asset is not a Sequence asset";
                return result;
            }
            const AssetGuid guid = record.guid;
            const std::filesystem::path sourcePath = record.sourcePath;
            SequenceAsset asset{};
            asset.guid = guid;
            asset.displayName = sequence.name;
            asset.sequence = sequence;
            std::string error{};
            if (!SaveSequenceAsset(
                    ResolveSourcePath(assetDatabase, record),
                    asset,
                    &error)) {
                result.message = std::move(error);
                return result;
            }
            AssetRecord updatedRecord = record;
            updatedRecord.displayName = sequence.name;
            updatedRecord.meta.displayName = sequence.name;
            (void)assetDatabase.WriteMeta(updatedRecord);
            (void)assetDatabase.ScanAssets(false);
            const bool imported = assetDatabase.ImportAsset(guid);
            result.success = imported;
            result.assetGuid = guid;
            result.sourcePath = sourcePath;
            result.message = imported
                ? "Sequence asset updated"
                : "Sequence saved, but asset validation failed";
            return result;
        }
    }

    SequenceAssetAuthoringResult CreateSequenceAsset(
        AssetDatabase& assetDatabase,
        const CinematicSequence& sequence) {

        SequenceAssetAuthoringResult result{};
        const std::filesystem::path directory =
            assetDatabase.GetAssetsRoot() / "Sequences";
        const std::string baseName = SanitizeFileStem(sequence.name);
        std::filesystem::path path = directory / (baseName + ".hsequence");
        for (uint32_t suffix = 2; std::filesystem::exists(path); ++suffix) {
            path = directory /
                (baseName + "_" + std::to_string(suffix) + ".hsequence");
        }

        SequenceAsset asset{};
        asset.displayName = sequence.name;
        asset.sequence = sequence;
        std::string error{};
        if (!SaveSequenceAsset(path, asset, &error)) {
            result.message = std::move(error);
            return result;
        }
        if (!assetDatabase.ScanAssets(true)) {
            result.message = "Sequence was written, but the asset scan failed";
            return result;
        }
        const AssetRecord* record = assetDatabase.FindByPath(path);
        if (record == nullptr || record->type != AssetType::Sequence ||
            !record->guid.IsValid()) {
            result.message = "Sequence was written, but no asset record was created";
            return result;
        }

        const AssetGuid guid = record->guid;
        const std::filesystem::path sourcePath = record->sourcePath;
        AssetRecord updatedRecord = *record;
        updatedRecord.displayName = sequence.name;
        updatedRecord.meta.displayName = sequence.name;
        (void)assetDatabase.WriteMeta(updatedRecord);
        asset.guid = guid;
        if (!SaveSequenceAsset(path, asset, &error)) {
            result.message = std::move(error);
            return result;
        }
        (void)assetDatabase.ScanAssets(false);
        result.success = assetDatabase.ImportAsset(guid);
        result.assetGuid = guid;
        result.sourcePath = sourcePath;
        result.message = result.success
            ? "Sequence asset created"
            : "Sequence asset created, but validation failed";
        return result;
    }

    SequenceAssetAuthoringResult UpdateSequenceAsset(
        AssetDatabase& assetDatabase,
        const AssetGuid& assetGuid,
        const CinematicSequence& sequence) {

        const AssetRecord* record = assetDatabase.FindByGuid(assetGuid);
        if (record == nullptr) {
            SequenceAssetAuthoringResult result{};
            result.message = "Selected Sequence asset was not found";
            return result;
        }
        return SaveToRecord(assetDatabase, *record, sequence);
    }

    bool LoadSequenceForAuthoring(
        const AssetDatabase& assetDatabase,
        const AssetGuid& assetGuid,
        CinematicSequence& outSequence,
        std::string& outMessage) {

        const AssetRecord* record = assetDatabase.FindByGuid(assetGuid);
        if (record == nullptr || record->type != AssetType::Sequence) {
            outMessage = "Selected asset is not a Sequence asset";
            return false;
        }
        SequenceAsset asset{};
        if (!LoadSequenceAsset(
                ResolveSourcePath(assetDatabase, *record),
                record->guid,
                asset,
                &outMessage)) {
            return false;
        }
        outSequence = std::move(asset.sequence);
        outMessage = "Sequence asset loaded into the active timeline";
        return true;
    }

} // namespace HIKARI::EDITOR
