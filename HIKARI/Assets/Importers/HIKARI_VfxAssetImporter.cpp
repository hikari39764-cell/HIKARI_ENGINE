#include "HIKARI_VfxAssetImporter.h"

#include <json.hpp>

namespace HIKARI {

    ASSETS::SEMANTICS::AssetImporterKind
        VfxAssetImporter::GetImporterKind() const noexcept {
        return ASSETS::SEMANTICS::AssetImporterKind::VfxEffect;
    }

    AssetMeta VfxAssetImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(
            sourcePath,
            guid,
            GetImporterKind());
        meta.importSettingsJson = nlohmann::json{
            { "sourceFormat", sourcePath.extension().string() },
            { "outputFormat", "HPAK" },
            { "cookVfx", false },
        }.dump(2);
        return meta;
    }

    AssetImportResult VfxAssetImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        (void)context;
        AssetImportResult result{};
        result.success = true;
        result.message = "[AssetImporter] VFX cook not implemented yet; existing Effekseer asset path remains authoritative. source=" +
            record.sourcePath.generic_string();
        return result;
    }

} // namespace HIKARI
