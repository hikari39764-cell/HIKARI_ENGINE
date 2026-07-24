#include "Assets/Importers/HIKARI_SequenceAssetImporter.h"

#include <algorithm>
#include <cctype>

#include <json.hpp>

#include "Assets/Sequence/HIKARI_SequenceAsset.h"
namespace HIKARI {

    ASSETS::SEMANTICS::AssetImporterKind
        SequenceAssetImporter::GetImporterKind() const noexcept {
        return ASSETS::SEMANTICS::AssetImporterKind::Sequence;
    }

    AssetMeta SequenceAssetImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(
            sourcePath,
            guid,
            GetImporterKind());
        meta.importSettingsJson = nlohmann::json{
            { "runtimeLoader", "SequenceAssetStore" },
            { "sourceAuthoritative", true }
        }.dump(2);
        return meta;
    }

    AssetImportResult SequenceAssetImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        AssetImportResult result{};
        SequenceAsset asset{};
        std::string error{};
        const std::filesystem::path sourcePath = record.sourcePath.is_absolute()
            ? record.sourcePath
            : (context.projectRoot / record.sourcePath).lexically_normal();
        result.success = LoadSequenceAsset(
            sourcePath,
            record.guid,
            asset,
            &error);
        result.message = result.success
            ? "Sequence asset validated"
            : "Sequence asset validation failed: " + error;
        result.diagnosticsJson = nlohmann::json{
            { "kind", "Sequence" },
            { "durationSeconds", asset.sequence.durationSeconds },
            { "bindingCount", asset.sequence.bindings.size() },
            { "valid", result.success }
        }.dump(2);
        return result;
    }

} // namespace HIKARI
