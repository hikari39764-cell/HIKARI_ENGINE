#pragma once

#include <string_view>

namespace HIKARI::ASSETS::MATERIAL {

// プロシージャルグラフィックス用マテリアルスロットの内置「デフォルト白」ID。
// AssetRegistryBuilder がランタイム MaterialAssetDescriptor として登録し、
// シーンシリアライズ時はこの ID を直接保存する（ディスク資産不要）。
inline constexpr std::string_view kBuiltInWhiteMaterialAssetId =
    "generated://solid/ffffffff";

// プロシージャルグラフィックス用の内置「チェッカー(デバッグ)」ID。
inline constexpr std::string_view kBuiltInCheckerMaterialAssetId =
    "generated://checker/bfbfbfff/8a8a8aff";

constexpr bool IsBuiltInWhiteMaterialAssetId(
    std::string_view id) noexcept {
    return id == kBuiltInWhiteMaterialAssetId;
}

constexpr bool IsBuiltInCheckerMaterialAssetId(
    std::string_view id) noexcept {
    return id == kBuiltInCheckerMaterialAssetId;
}

} // namespace HIKARI::ASSETS::MATERIAL
