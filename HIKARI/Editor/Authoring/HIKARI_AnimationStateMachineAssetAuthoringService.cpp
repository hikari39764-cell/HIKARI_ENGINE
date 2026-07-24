#include "Editor/Authoring/HIKARI_AnimationStateMachineAssetAuthoringService.h"

#include <algorithm>
#include <cctype>

#include "Assets/Animation/HIKARI_AnimationStateMachineAsset.h"
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
            return value.empty() ? "AnimationStateMachine" : value;
        }

        std::filesystem::path ResolveSourcePath(
            const AssetDatabase& database,
            const AssetRecord& record) {
            return record.sourcePath.is_absolute()
                ? record.sourcePath
                : (database.GetProjectRoot() / record.sourcePath)
                    .lexically_normal();
        }

        AnimationStateMachineAssetAuthoringResult SaveToRecord(
            AssetDatabase& database,
            const AssetRecord& record,
            const ANIMATION::AnimationStateMachineDefinition& definition) {
            AnimationStateMachineAssetAuthoringResult result{};
            if (record.type != AssetType::AnimationStateMachine ||
                !record.guid.IsValid()) {
                result.message =
                    "Selected asset is not an Animation State Machine";
                return result;
            }
            AnimationStateMachineAsset asset{};
            asset.guid = record.guid;
            asset.displayName = definition.name;
            asset.definition = definition;
            std::string error{};
            if (!SaveAnimationStateMachineAsset(
                    ResolveSourcePath(database, record), asset, &error)) {
                result.message = std::move(error);
                return result;
            }
            AssetRecord updated = record;
            updated.displayName = definition.name;
            updated.meta.displayName = definition.name;
            (void)database.WriteMeta(updated);
            (void)database.ScanAssets(false);
            result.success = database.ImportAsset(record.guid);
            result.assetGuid = record.guid;
            result.sourcePath = record.sourcePath;
            result.message = result.success
                ? "Animation state machine saved"
                : "Asset saved, but validation failed";
            return result;
        }
    }

    AnimationStateMachineAssetAuthoringResult
        CreateAnimationStateMachineAsset(
            AssetDatabase& database,
            const ANIMATION::AnimationStateMachineDefinition& definition) {
        AnimationStateMachineAssetAuthoringResult result{};
        const std::filesystem::path directory =
            database.GetAssetsRoot() / "AnimationStateMachines";
        const std::string baseName = SanitizeFileStem(definition.name);
        std::filesystem::path path = directory / (baseName + ".hanimsm");
        for (uint32_t suffix = 2u; std::filesystem::exists(path); ++suffix) {
            path = directory /
                (baseName + "_" + std::to_string(suffix) + ".hanimsm");
        }
        AnimationStateMachineAsset asset{};
        asset.displayName = definition.name;
        asset.definition = definition;
        std::string error{};
        if (!SaveAnimationStateMachineAsset(path, asset, &error)) {
            result.message = std::move(error);
            return result;
        }
        if (!database.ScanAssets(true)) {
            result.message = "Asset was written, but the scan failed";
            return result;
        }
        const AssetRecord* record = database.FindByPath(path);
        if (record == nullptr ||
            record->type != AssetType::AnimationStateMachine ||
            !record->guid.IsValid()) {
            result.message = "Asset record was not created";
            return result;
        }
        asset.guid = record->guid;
        if (!SaveAnimationStateMachineAsset(path, asset, &error)) {
            result.message = std::move(error);
            return result;
        }
        return SaveToRecord(database, *record, definition);
    }

    AnimationStateMachineAssetAuthoringResult
        UpdateAnimationStateMachineAsset(
            AssetDatabase& database,
            const AssetGuid& assetGuid,
            const ANIMATION::AnimationStateMachineDefinition& definition) {
        const AssetRecord* record = database.FindByGuid(assetGuid);
        if (record == nullptr) {
            AnimationStateMachineAssetAuthoringResult result{};
            result.message = "Animation state machine asset was not found";
            return result;
        }
        return SaveToRecord(database, *record, definition);
    }

    bool LoadAnimationStateMachineForAuthoring(
        const AssetDatabase& database,
        const AssetGuid& assetGuid,
        ANIMATION::AnimationStateMachineDefinition& outDefinition,
        std::string& outMessage) {
        const AssetRecord* record = database.FindByGuid(assetGuid);
        if (record == nullptr ||
            record->type != AssetType::AnimationStateMachine) {
            outMessage = "Selected asset is not an Animation State Machine";
            return false;
        }
        AnimationStateMachineAsset asset{};
        if (!LoadAnimationStateMachineAsset(
                ResolveSourcePath(database, *record),
                record->guid,
                asset,
                &outMessage)) {
            return false;
        }
        outDefinition = std::move(asset.definition);
        outMessage = "Animation state machine opened";
        return true;
    }

} // namespace HIKARI::EDITOR
