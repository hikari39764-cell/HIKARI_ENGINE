#pragma once

namespace HIKARI {
class AssetImporterRegistry;
// バイナリ生成を持たない構造化アセットの登録。各形式の意味は専用ローダーが所有する。
void RegisterStructuredAssetImporters(AssetImporterRegistry& registry);
} // namespace HIKARI
