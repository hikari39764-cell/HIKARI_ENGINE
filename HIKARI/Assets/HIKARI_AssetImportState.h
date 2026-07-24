#pragma once

namespace HIKARI {

    struct AssetRecord;

    enum class AssetImportState {
        Unknown,
        MetaOnly,
        Imported,
        Outdated,
        MissingSource,
        MissingMeta,
        MissingArtifact,
        UnknownImporter,
        DuplicateGuid,
        ImportFailed,
    };

    AssetImportState GetImportState(const AssetRecord& record);
    bool IsBrokenAssetRecord(const AssetRecord& record);
    const char* ToString(AssetImportState state);

} // namespace HIKARI
