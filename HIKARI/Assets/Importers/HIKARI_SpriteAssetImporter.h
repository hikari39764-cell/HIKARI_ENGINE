#pragma once

#include <memory>

#include "Assets/Importers/HIKARI_AssetImporter.h"
#include "Assets/Importers/HIKARI_TextureImportBackend.h"

namespace HIKARI {

class SpriteAssetImporter final : public IAssetImporter {
public:
    explicit SpriteAssetImporter(std::unique_ptr<ITextureImportBackend> backend);

    ASSETS::SEMANTICS::AssetImporterKind GetImporterKind() const noexcept override;
    AssetMeta CreateDefaultMeta(const std::filesystem::path& sourcePath, const AssetGuid& guid) const override;
    AssetImportResult Import(const AssetRecord& record, const AssetImportContext& context) override;

private:
    std::unique_ptr<ITextureImportBackend> backend_{};
};

} // namespace HIKARI
