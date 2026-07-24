#pragma once

#include <memory>

#include "HIKARI_AssetImporter.h"
#include "HIKARI_TextureImportBackend.h"

namespace HIKARI {

    class SkyCubemapImporter final : public IAssetImporter {
    public:
        explicit SkyCubemapImporter(std::unique_ptr<ITextureImportBackend> backend);

        ASSETS::SEMANTICS::AssetImporterKind GetImporterKind()
            const noexcept override;

        AssetMeta CreateDefaultMeta(
            const std::filesystem::path& sourcePath,
            const AssetGuid& guid) const override;

        AssetImportResult Import(
            const AssetRecord& record,
            const AssetImportContext& context) override;

    private:
        std::unique_ptr<ITextureImportBackend> backend_{};
    };

} // namespace HIKARI
