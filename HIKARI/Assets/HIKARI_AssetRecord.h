#pragma once

#include <filesystem>
#include <string>

#include "HIKARI_AssetArtifactManifest.h"
#include "HIKARI_AssetMeta.h"

namespace HIKARI {

    struct AssetRecord {
        AssetGuid guid{};
        AssetType type = AssetType::Unknown;

        std::filesystem::path sourcePath{};
        std::filesystem::path metaPath{};
        std::filesystem::path artifactManifestPath{};
        std::filesystem::path importedDirectory{};

        std::string displayName{};

        bool sourceExists = false;
        bool metaExists = false;
        bool importOutdated = false;
        bool lastImportSucceeded = false;

        bool duplicateGuid = false;
        bool importerMissing = false;
        bool artifactMissing = false;
        std::string lastImportMessage{};

        AssetMeta meta{};
        AssetArtifactManifest artifactManifest{};
    };

    inline std::string GetAssetRecordDisplayName(const AssetRecord& record) {
        if (!record.displayName.empty()) {
            return record.displayName;
        }
        std::string name = record.sourcePath.stem().string();
        return name.empty() ? record.guid.value : name;
    }

} // namespace HIKARI
