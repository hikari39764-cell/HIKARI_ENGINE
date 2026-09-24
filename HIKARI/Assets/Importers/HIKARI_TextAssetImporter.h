#pragma once
#include "Assets/Importers/HIKARI_AssetImporter.h"
namespace HIKARI {
// フォントと文字スタイルは同じ資産経路を使い、描画キャッシュはレンダラが所有する。
class TextAssetImporter final : public IAssetImporter {
public:
    explicit TextAssetImporter(bool style) : style_(style) {}
    ASSETS::SEMANTICS::AssetImporterKind GetImporterKind() const noexcept override;
    AssetMeta CreateDefaultMeta(const std::filesystem::path&, const AssetGuid&) const override;
    AssetImportResult Import(const AssetRecord&, const AssetImportContext&) override;

private:
    bool style_;
};
} // namespace HIKARI
