#include "Editor/Assets/HIKARI_AssetImportSettingsService.h"

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportInputs.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"

namespace HIKARI::EDITOR {
bool AssetImportSettingsDraft::IsDirty() const {
    return edited.displayName != baseline.displayName ||
           CanonicalImportSettings(edited.importSettingsJson) != CanonicalImportSettings(baseline.importSettingsJson);
}

void AssetImportSettingsDraft::Reset(const AssetRecord& record) {
    baseline = record.meta;
    edited = baseline;
}

EditorOperationResult ApplyAssetImportSettings(AssetDatabase& database, const AssetGuid& guid,
                                               AssetImportSettingsDraft& draft) {
    if (database.GetQueuedImportStatus().active)
        return OperationFailure("IMPORT_BUSY", "Wait for the current import before applying settings.");
    std::string error;
    if (!database.UpdateImportSettings(guid, draft.baseline, draft.edited, error))
        return OperationFailure("IMPORT_SETTINGS_FAILED", std::move(error));
    draft.Reset(*database.FindByGuid(guid));
    if (!database.QueueImportAssets({guid}, "Apply import settings"))
        return OperationFailure("IMPORT_QUEUE_FAILED",
                                "Settings saved, but import could not be queued. Retry reimport.");
    auto result = OperationOk("Settings saved; import queued. Check the import result.");
    result.code = "PENDING";
    result.data = {{"guid", guid.value}, {"queued", true}};
    return result;
}

void RegisterAssetImportSettingsOperations(EditorOperationRegistry& registry,
                                           std::function<AssetDatabase*()> databaseProvider) {
    EditorOperationDefinition query;
    query.id = "asset.import_settings";
    query.summary = "Read saved import settings and the reason an asset needs reimport.";
    query.workspace = "assets";
    query.parameters = {{"guid", EditorOperationParameterType::String, true, "Asset GUID"}};
    query.permission = EditorOperationPermission::ReadEditorState;
    query.execute = [databaseProvider](const nlohmann::json& parameters) {
        const auto* database = databaseProvider();
        const auto* record = database ? database->FindByGuid({parameters.at("guid").get<std::string>()}) : nullptr;
        if (!record)
            return OperationFailure("ASSET_NOT_FOUND", "Asset is unavailable.");
        return OperationOk("Saved import settings",
                           {{"settings", nlohmann::json::parse(CanonicalImportSettings(record->meta.importSettingsJson),
                                                               nullptr, false)},
                            {"displayName", record->meta.displayName},
                            {"outdated", record->importOutdated},
                            {"reason", record->importOutdatedReason}});
    };
    registry.Register(std::move(query), EditorOperationKind::Query, false);

    EditorOperationDefinition apply;
    apply.id = "asset.apply_import_settings";
    apply.summary = "Apply import settings and queue reimport, rejecting external changes and active imports.";
    apply.workspace = "assets";
    apply.parameters = {
        {"guid", EditorOperationParameterType::String, true, "Asset GUID"},
        {"expectedSettings", EditorOperationParameterType::Json, true, "Previously read settings"},
        {"expectedDisplayName", EditorOperationParameterType::String, true, "Previously read display name"},
        {"settings", EditorOperationParameterType::Json, true, "New settings object"},
        {"displayName", EditorOperationParameterType::String, true, "New display name"}};
    apply.permission = EditorOperationPermission::ModifyProjectFiles;
    apply.execute = [databaseProvider](const nlohmann::json& parameters) {
        auto* database = databaseProvider();
        const AssetGuid guid{parameters.at("guid").get<std::string>()};
        const auto* record = database ? database->FindByGuid(guid) : nullptr;
        if (!record)
            return OperationFailure("ASSET_NOT_FOUND", "Asset is unavailable.");
        AssetImportSettingsDraft draft;
        draft.Reset(*record);
        draft.baseline.importSettingsJson = parameters.at("expectedSettings").dump();
        draft.baseline.displayName = parameters.at("expectedDisplayName").get<std::string>();
        draft.edited.importSettingsJson = parameters.at("settings").dump();
        draft.edited.displayName = parameters.at("displayName").get<std::string>();
        return ApplyAssetImportSettings(*database, guid, draft);
    };
    registry.Register(std::move(apply), EditorOperationKind::Mutation, false);
}
} // namespace HIKARI::EDITOR
