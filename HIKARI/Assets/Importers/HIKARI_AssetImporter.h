#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetArtifactManifest.h"
#include "Assets/HIKARI_AssetMeta.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Assets/Tasks/HIKARI_AssetImportPublication.h"

namespace HIKARI {

class AssetTaskContext;
class AssetDatabase;

struct AssetImportContext {
    std::filesystem::path projectRoot{};
    std::filesystem::path assetsRoot{};
    std::filesystem::path libraryRoot{};
    std::filesystem::path sourceMetaRoot{};
    std::filesystem::path importedDirectory{};
    const AssetDatabase* assetDatabase = nullptr;
    AssetTaskContext* task = nullptr;
    std::shared_ptr<AssetImportPublication> publication{};

    std::filesystem::path StageFile(const std::filesystem::path& finalPath) const {
        return publication ? publication->StageFile(finalPath) : finalPath;
    }
};

struct AssetImportResult {
    bool success = false;
    std::string message{};
    std::string diagnosticsJson{};
    std::vector<AssetArtifactDesc> artifacts{};
    std::vector<AssetDependencyDesc> dependencies{};
    AssetImportInputs inputs{};
    std::shared_ptr<AssetImportPublication> publication{};
};

class IAssetImporter {
public:
    virtual ~IAssetImporter() = default;

    virtual ASSETS::SEMANTICS::AssetImporterKind GetImporterKind() const noexcept = 0;

    const ASSETS::SEMANTICS::AssetImporterSemantics& GetSemantics() const noexcept {
        return ASSETS::SEMANTICS::GetAssetImporterSemantics(GetImporterKind());
    }

    virtual AssetMeta CreateDefaultMeta(const std::filesystem::path& sourcePath, const AssetGuid& guid) const = 0;

    virtual AssetImportResult Import(const AssetRecord& record, const AssetImportContext& context) = 0;
};

} // namespace HIKARI
