#include "Editor/Authoring/HIKARI_StructuredAssetAuthoringService.h"
#include "Assets/Animation/HIKARI_AnimationStateMachineAsset.h"
#include "Assets/Dialogue/HIKARI_DialogueAsset.h"
#include "Assets/Sequence/HIKARI_SequenceAsset.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"

namespace HIKARI::EDITOR {
namespace {
template <class Asset, class Definition>
struct AssetAuthoringFormat {
    AssetType type;
    const char* directory;
    const char* stem;
    const char* extension;
    Definition Asset::* payload;
    bool (*load)(const std::filesystem::path&, const AssetGuid&, Asset&, std::string*);
    bool (*save)(const std::filesystem::path&, const Asset&, std::string*);
};

template <class Asset, class Definition>
StructuredAssetAuthoringResult Save(AssetDatabase& database, const AssetGuid& guid, const Definition& definition,
                                    const AssetAuthoringFormat<Asset, Definition>& format) {
    StructuredAssetAuthoringResult result;
    Asset asset;
    asset.displayName = definition.name;
    asset.*format.payload = definition;
    AssetRecord record;
    std::filesystem::path path;
    if (guid.IsValid()) {
        const auto* found = database.FindByGuid(guid);
        if (!found || found->type != format.type) {
            result.message = "Selected asset was not found or has the wrong type";
            return result;
        }
        record = *found;
        path = (database.GetProjectRoot() / record.sourcePath).lexically_normal();
    } else {
        const auto directory = database.GetAssetsRoot() / format.directory;
        const auto stem = ASSETS::SEMANTICS::MakeAssetFileStem(definition.name, format.stem);
        path = directory / (stem + format.extension);
        for (unsigned suffix = 2; std::filesystem::exists(path); ++suffix)
            path = directory / (stem + "_" + std::to_string(suffix) + format.extension);
        if (!format.save(path, asset, &result.message))
            return result;
        if (!database.ScanAssets(true)) {
            result.message = "Asset written, but asset scan failed";
            return result;
        }
        const auto* found = database.FindByPath(path);
        if (!found || found->type != format.type || !found->guid.IsValid()) {
            result.message = "Asset written, but asset metadata was not created";
            return result;
        }
        record = *found;
    }
    // Scan/Import はレコードを置き換えるため、参照ではなくコピーを保持する。
    asset.guid = record.guid;
    result.assetGuid = record.guid;
    result.sourcePath = record.sourcePath;
    if (!format.save(path, asset, &result.message))
        return result;
    record.displayName = definition.name;
    record.meta.displayName = definition.name;
    if (!database.WriteMeta(record)) {
        result.message = "Asset saved, but metadata update failed";
        return result;
    }
    if (!database.ScanAssets(false)) {
        result.message = "Asset saved, but asset scan failed";
        return result;
    }
    result.success = database.ImportAsset(result.assetGuid);
    result.message = result.success ? "Asset saved" : "Asset saved, but validation failed";
    return result;
}

template <class Asset, class Definition>
bool Load(const AssetDatabase& database, const AssetGuid& guid, Definition& definition, std::string& message,
          const AssetAuthoringFormat<Asset, Definition>& format) {
    const auto* record = database.FindByGuid(guid);
    if (!record || record->type != format.type) {
        message = "Selected asset was not found or has the wrong type";
        return false;
    }
    Asset asset;
    if (!format.load((database.GetProjectRoot() / record->sourcePath).lexically_normal(), record->guid, asset,
                     &message))
        return false;
    definition = std::move(asset.*format.payload);
    message = "Asset loaded";
    return true;
}
const AssetAuthoringFormat<AnimationStateMachineAsset, ANIMATION::AnimationStateMachineDefinition>
    kAnimationStateMachine{AssetType::AnimationStateMachine,
                           "AnimationStateMachines",
                           "AnimationStateMachine",
                           ".hanimsm",
                           &AnimationStateMachineAsset::definition,
                           LoadAnimationStateMachineAsset,
                           SaveAnimationStateMachineAsset};
const AssetAuthoringFormat<DialogueAsset, DIALOGUE::DialogueDefinition> kDialogue{
    AssetType::Dialogue,        "Dialogues",       "Dialogue",       ".hdialogue",
    &DialogueAsset::definition, LoadDialogueAsset, SaveDialogueAsset};
const AssetAuthoringFormat<SequenceAsset, CinematicSequence> kSequence{
    AssetType::Sequence,      "Sequences",       "Sequence",       ".hsequence",
    &SequenceAsset::sequence, LoadSequenceAsset, SaveSequenceAsset};
} // namespace
StructuredAssetAuthoringResult CreateAnimationStateMachineAsset(
    AssetDatabase& database, const ANIMATION::AnimationStateMachineDefinition& definition) {
    return Save(database, {}, definition, kAnimationStateMachine);
}
StructuredAssetAuthoringResult UpdateAnimationStateMachineAsset(
    AssetDatabase& database, const AssetGuid& guid, const ANIMATION::AnimationStateMachineDefinition& definition) {
    if (!guid.IsValid())
        return {false, {}, {}, "Invalid asset identity"};
    return Save(database, guid, definition, kAnimationStateMachine);
}
bool LoadAnimationStateMachineForAuthoring(const AssetDatabase& database, const AssetGuid& guid,
                                           ANIMATION::AnimationStateMachineDefinition& definition,
                                           std::string& message) {
    return Load(database, guid, definition, message, kAnimationStateMachine);
}
StructuredAssetAuthoringResult CreateDialogueAsset(AssetDatabase& database,
                                                   const DIALOGUE::DialogueDefinition& definition) {
    return Save(database, {}, definition, kDialogue);
}
StructuredAssetAuthoringResult UpdateDialogueAsset(AssetDatabase& database, const AssetGuid& guid,
                                                   const DIALOGUE::DialogueDefinition& definition) {
    if (!guid.IsValid())
        return {false, {}, {}, "Invalid asset identity"};
    return Save(database, guid, definition, kDialogue);
}
bool LoadDialogueForAuthoring(const AssetDatabase& database, const AssetGuid& guid,
                              DIALOGUE::DialogueDefinition& definition, std::string& message) {
    return Load(database, guid, definition, message, kDialogue);
}
StructuredAssetAuthoringResult CreateSequenceAsset(AssetDatabase& database, const CinematicSequence& definition) {
    return Save(database, {}, definition, kSequence);
}
StructuredAssetAuthoringResult UpdateSequenceAsset(AssetDatabase& database, const AssetGuid& guid,
                                                   const CinematicSequence& definition) {
    if (!guid.IsValid())
        return {false, {}, {}, "Invalid asset identity"};
    return Save(database, guid, definition, kSequence);
}
bool LoadSequenceForAuthoring(const AssetDatabase& database, const AssetGuid& guid, CinematicSequence& definition,
                              std::string& message) {
    return Load(database, guid, definition, message, kSequence);
}
} // namespace HIKARI::EDITOR
