#pragma once

#include "Assets/Importers/HIKARI_AssetImporter.h"

namespace HIKARI {

class PrefabAssetImporter final : public IAssetImporter {
public:
    ASSETS::SEMANTICS::AssetImporterKind GetImporterKind() const noexcept override;
    AssetMeta CreateDefaultMeta(const std::filesystem::path& sourcePath, const AssetGuid& guid) const override;
    AssetImportResult Import(const AssetRecord& record, const AssetImportContext& context) override;
};

} // namespace HIKARI
