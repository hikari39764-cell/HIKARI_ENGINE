#include "HIKARI_SceneAssetImporter.h"

#include <json.hpp>

namespace HIKARI {

    ASSETS::SEMANTICS::AssetImporterKind
        SceneAssetImporter::GetImporterKind() const noexcept {
        return ASSETS::SEMANTICS::AssetImporterKind::Scene;
    }

    AssetMeta SceneAssetImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(
            sourcePath,
            guid,
            GetImporterKind());
        meta.importSettingsJson = nlohmann::json{
            { "sourceFormat", sourcePath.extension().string() },
            { "runtimeLoader", "SceneSerializer" },
            { "cookScene", false },
            { "outputFormat", "HSCENE" },
        }.dump(2);
        return meta;
    }

    AssetImportResult SceneAssetImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        (void)context;
        AssetImportResult result{};
        result.success = true;
        result.message = "[AssetImporter] Scene cook not implemented yet; scene JSON remains authoritative. source=" +
            record.sourcePath.generic_string();
        result.diagnosticsJson = nlohmann::json{
            { "kind", "Scene" },
            { "runtimeLoader", "SceneSerializer" },
            { "cookScene", false },
        }.dump(2);
        return result;
    }

} // namespace HIKARI
